// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (C) 2018 Red Hat, Inc.
 * Contributor : Matt Benjamin <mbenjamin@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <random>
#include "gtest/gtest.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/program_options.hpp>

extern "C" {

#include "city.h"
#include <intrinsic.h>
#include "nfs_dupreq.h"
#include "nfs_core.h"

/* LTTng headers */
#include <lttng/lttng.h>

/* gperf headers */
#include <gperftools/profiler.h>

  extern const nfs_function_desc_t nfs3_func_desc[];


} /* extern "C" */

namespace {

  struct svc_xprt global_xprt;

  const char* remote_addr = "10.1.1.1";
  uint16_t remote_port = 45000;
  char* profile_out = nullptr;
  uint32_t nthreads = 2;
  bool cityhash = false;
  bool per_thread_xprt = false;
  bool verbose = false;
  uint32_t item_wsize = 1000; // must be larger than drc_size!
  uint32_t num_calls = 1000000;
  uint32_t drc_parts = 1;
  uint32_t drc_lanes = 3;
  uint32_t drc_size = 1024;
  uint32_t drc_hiwat = 128;
  uint32_t drc_cache = 1; /* XXXX 0 can crash */

  class NFSRequest
  {
  public:
    std::string fh;
    request_data_t req_data;
    nfs_request_t& req;
    struct svc_req& svc;
    uint32_t xid_off;

  NFSRequest(struct svc_xprt *_xprt, uint32_t _xid_off)
    : req(req_data.r_u.req), svc(req.svc), xid_off(_xid_off) {
      memset(&req_data, 0, sizeof(request_data_t));
      req_data.rtype = NFS_REQUEST;
      req.svc.rq_xprt = _xprt;
    }

    nfs_request_t* get_nfs_req() {
      return &req;
    }

    struct svc_req* get_svc_req() {
      return &req.svc;
    }

    void update_v3_write(uint32_t xid_ix) {
      struct svc_req* svc = get_svc_req();
      svc->rq_msg.rm_xid = xid_off + xid_ix;
      svc->rq_cksum = (cityhash) ?
	CityHash64((char *)&svc->rq_msg.rm_xid, sizeof(svc->rq_msg.rm_xid))
	: svc->rq_msg.rm_xid;
    }

  };

  NFSRequest* forge_v3_write(struct svc_xprt *_xprt, std::string fh,
			    uint32_t xid_off, uint32_t off, uint32_t len) {
    NFSRequest* req = new NFSRequest(_xprt, xid_off);
    req->fh = fh;

    struct svc_req* svc = req->get_svc_req();
    svc->rq_msg.cb_prog = 100003;
    svc->rq_msg.cb_vers = 3;
    svc->rq_msg.cb_proc = NFSPROC3_WRITE;

    nfs_request_t* nfs = /* req->get_nfs_req() */ &req->req;
    nfs->funcdesc = &nfs3_func_desc[NFSPROC3_WRITE];

    WRITE3args* arg_write3 = (WRITE3args*) &nfs->arg_nfs;
    arg_write3->file.data.data_len = req->fh.length();
    arg_write3->file.data.data_val = const_cast<char*>(req->fh.data());
    arg_write3->offset = off;
    arg_write3->count = len;
    arg_write3->stable = DATA_SYNC;
    /* leave data nil for now */

    return req;
  }

  uint32_t xid_ix;
  std::atomic<uint32_t> threads_started{0};
  std::mutex mtx;
  std::condition_variable start_cond;

  class Worker
  {
  public:
    uint32_t thr_ix;
    NFSRequest** req_arr;
    struct timespec s_time;
    struct timespec e_time;
    struct svc_xprt xprt;

    Worker() = delete;

  Worker(uint32_t thr_ix)
    : thr_ix(thr_ix) {

      /* ips counting from "33.249.130.128" */
      xprt.xp_type = XPRT_TCP;
      struct sockaddr_in* sin = (struct sockaddr_in *) &xprt.xp_remote.ss;
      sin->sin_family = AF_INET;
      sin->sin_port = htons(remote_port);

      uint32_t ipv = htonl(570000000 + thr_ix);
      unsigned char *ip = reinterpret_cast<unsigned char*>(&ipv);
      std::string remote_addr =
	std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "."
	+ std::to_string(ip[2]) + "." + std::to_string(ip[3]);

      inet_pton(AF_INET, remote_addr.c_str(), &sin->sin_addr);
      __rpc_address_setup(&xprt.xp_remote);

      this->req_arr = new NFSRequest*[item_wsize];
      uint32_t xid_off = num_calls * thr_ix;
      for (uint32_t ix = 0; ix < item_wsize; ++ix) {
	struct svc_xprt *special_xprt =
	  (per_thread_xprt)
	  ? &xprt
	  : &global_xprt;
	NFSRequest* cc_req =
	  forge_v3_write(special_xprt, "file1", xid_off, 96 /* offset */,
			65535 /* len */);
	if (! cc_req)
	  abort();
	this->req_arr[ix] = cc_req;
	if (verbose) {
	  std::cout
	    << " thread: " << thr_ix
	    << " elt: " << ix
	    << " NFSRequest: " << this->req_arr[ix]
	    << std::endl;
	}
      }
    }

    void operator()() {
      int r = 0;

      ++threads_started;
      if (nthreads > 0) {
	std::unique_lock<std::mutex> guard(mtx);
	start_cond.wait(guard);
	guard.unlock();
      }

      now(&s_time);

      uint32_t req_ix = 0;
      for (uint32_t call_ix = 0; call_ix < num_calls; ++call_ix) {
	NFSRequest* cc_req = req_arr[req_ix];
	cc_req->update_v3_write(call_ix /* new xid basis */);

	if (verbose) {
	  std::cout
	    << " thread: " << thr_ix
	    << " call: " << call_ix
	    << " NFSRequest: " << cc_req
	    << std::endl;
	}

	nfs_request_t* reqnfs = &cc_req->req;
	struct svc_req* req = cc_req->get_svc_req();

	r = nfs_dupreq_start(reqnfs, req);
	if (! req->rq_u1) {
	  abort();
	}
	r = nfs_dupreq_finish(req, NULL);
	if (! req->rq_u1) {
	  abort();
	}
	nfs_dupreq_rele(req, NULL);
	if (! req->rq_u1) {
	  abort();
	}

	/* wrap req */
	++req_ix;
	if (req_ix == item_wsize) {
	  req_ix = 0;
	}
      } /* call_ix */

      now(&e_time);
    } /* () */

    ~Worker() {
      for (uint32_t ix = 0; ix < item_wsize; ++ix) {
	delete this->req_arr[ix];
      }
      delete[] this->req_arr;
    }

  };

  class DRCLatency1 : public ::testing::Test {
  public:
    std::vector<Worker*> workers;

    virtual void SetUp() {

      global_xprt.xp_type = XPRT_TCP;
      struct sockaddr_in* sin =
	(struct sockaddr_in *) &global_xprt.xp_remote.ss;
      sin->sin_family = AF_INET;
      sin->sin_port = htons(remote_port);
      inet_pton(AF_INET, remote_addr, &sin->sin_addr);
      __rpc_address_setup(&global_xprt.xp_remote);

      for (uint32_t ix = 0; ix < nthreads; ++ix) {
	auto worker = new Worker(ix);
	workers.push_back(worker);
      }

      /* avoid crash in shared drc */
      nfs_param.core_param.drc.udp.nlane = 1;

      /* setup TCP DRC */
      nfs_param.core_param.drc.disabled = false;
      nfs_param.core_param.drc.tcp.npart = drc_parts;
      nfs_param.core_param.drc.tcp.nlane = drc_lanes;
      nfs_param.core_param.drc.tcp.size = drc_size;
      nfs_param.core_param.drc.tcp.cachesz = drc_cache;
      nfs_param.core_param.drc.tcp.hiwat = drc_hiwat;
      nfs_param.core_param.drc.tcp.recycle_npart = DRC_TCP_RECYCLE_NPART;
      nfs_param.core_param.drc.tcp.recycle_expire_s = 600;

      dupreq2_pkginit();
    }

    virtual void TearDown() {
      if (nthreads > 0) {
	for (auto& worker : workers) {
	  delete worker;
	}
      }
    }

  };

} /* namespace */

TEST_F(DRCLatency1, RUN1) {

  uint16_t eff_threads = 1;

  if (profile_out)
    ProfilerStart(profile_out);

  if (nthreads > 0) {
    std::vector<std::thread> threads;
    threads.reserve(nthreads);
    for (auto& worker : workers) {
      threads.emplace_back(std::ref(*worker));
    }

    eff_threads = nthreads;

    struct timespec ts = {0, 50000000 };
    nanosleep(&ts, nullptr);
    while (threads_started < nthreads) {
      nanosleep(&ts, nullptr);
    }
    start_cond.notify_all();

    for (auto &t : threads)
      t.join();
  } else {
    /* run in main() thread only */
    Worker worker(0);
    worker.operator()();
    workers.push_back(&worker);
  }

  if (profile_out)
    ProfilerStop();

  /* total time, all threads */
  uint64_t dt = 0;
  for (const auto& worker : workers) {
    dt += timespec_diff(&worker->s_time, &worker->e_time);
  }

  uint64_t reqs_s = (eff_threads * num_calls) / (double(dt) / 1000000000);

  fprintf(stderr, "total run time: %" PRIu64 " (" PRIu64 " reqs %" PRIu64
	  " reqs/s, %d threads) \n", dt, reqs_s, nthreads);

} /* TEST_F(DRCLatency1, RUN1) */

int main(int argc, char *argv[])
{
  using namespace std;
  using namespace std::literals;
  namespace po = boost::program_options;

  int code = 0;

  po::options_description opts("program options");
  po::variables_map vm;

  try {

    opts.add_options()
      ("profile", po::value<string>(),
        "path to google perf output file")

      ("nthreads", po::value<uint16_t>(),
        "number of threads")

      ("verbose", po::bool_switch(&verbose),
        "verbose output (default off)")

      ("hash_xids", po::bool_switch(&cityhash),
        "hash xid as value of checksum (defaults to xid)")

      ("per_thread_xprt", po::bool_switch(&per_thread_xprt),
        "create an RPC xprt handle per thread (default global)")

      ("wsize", po::value<uint32_t>(),
        "number of requests in per-thread work array (default 1M)")

      ("ncalls", po::value<uint32_t>(),
        "number of calls per-thread (default 1M)")

      ("nparts", po::value<uint16_t>(),
        "number of tree partitions per TCP DRC lane")

      ("nlanes", po::value<uint16_t>(),
        "number of TCP DRC lanes per DRC")

      ("dsize", po::value<uint16_t>(),
        "max unretired entries in TCP DRC (default 1024)")

      ("dhiwat", po::value<uint16_t>(),
        "TCP DRC high water mark (default 1028)")

      ("dcache", po::value<uint16_t>(),
        "size of tree cache in TCP DRC (default 1)")

      ;

    po::variables_map::iterator vm_iter;
    po::command_line_parser parser{argc, argv};
    parser.options(opts).allow_unregistered();
    po::store(parser.run(), vm);
    po::notify(vm);

    // use config vars--leaves them on the stack
    vm_iter = vm.find("profile");
    if (vm_iter != vm.end()) {
      profile_out = (char*) vm_iter->second.as<std::string>().c_str();
    }

    vm_iter = vm.find("nthreads");
    if (vm_iter != vm.end()) {
      nthreads = vm_iter->second.as<std::uint16_t>();
    }

    vm_iter = vm.find("wsize");
    if (vm_iter != vm.end()) {
      item_wsize = vm_iter->second.as<std::uint32_t>();
    }

    vm_iter = vm.find("ncalls");
    if (vm_iter != vm.end()) {
      num_calls = vm_iter->second.as<std::uint32_t>();
    }

    vm_iter = vm.find("nparts");
    if (vm_iter != vm.end()) {
      drc_parts = vm_iter->second.as<std::uint16_t>();
    }

    vm_iter = vm.find("nlanes");
    if (vm_iter != vm.end()) {
      drc_lanes = vm_iter->second.as<std::uint16_t>();
    }

    vm_iter = vm.find("dsize");
    if (vm_iter != vm.end()) {
      drc_size = vm_iter->second.as<std::uint16_t>();
    }

    vm_iter = vm.find("dhiwat");
    if (vm_iter != vm.end()) {
      drc_hiwat = vm_iter->second.as<std::uint16_t>();
    }

    vm_iter = vm.find("dcache");
    if (vm_iter != vm.end()) {
      drc_cache = vm_iter->second.as<std::uint16_t>();
    }

    ::testing::InitGoogleTest(&argc, argv);

    code  = RUN_ALL_TESTS();
  } /* try */

  catch(po::error& e) {
    cout << "Error parsing opts " << e.what() << endl;
  }

  catch(...) {
    cout << "Unhandled exception in main()" << endl;
  }

  return code;
}
