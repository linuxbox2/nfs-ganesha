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

  struct svc_xprt xprt;

  const char* remote_addr = "10.1.1.1";
  uint16_t remote_port = 45000;
  char* profile_out = nullptr; //"/tmp/profile.out";
  uint32_t nthreads = 2;

  class NFSRequest
  {
  public:
    std::string fh;
    request_data_t req_data;
    nfs_request_t& req;
    struct svc_req& svc;

    NFSRequest()
      : req(req_data.r_u.req), svc(req.svc) {
      memset(&req_data, 0, sizeof(request_data_t));
      req_data.rtype = NFS_REQUEST;
      req.svc.rq_xprt = &xprt;
    }

    nfs_request_t* get_nfs_req() {
      return &req;
    }

    struct svc_req* get_svc_req() {
      return &req.svc;
    }
  };

  NFSRequest* forge_v3_write(std::string fh, uint32_t xid, uint32_t off,
			    uint32_t len) {
    NFSRequest* req = new NFSRequest();
    req->fh = fh;

    struct svc_req* svc = req->get_svc_req();
    svc->rq_msg.rm_xid = xid;
    svc->rq_msg.cb_prog = 100003;
    svc->rq_msg.cb_vers = 3;
    svc->rq_msg.cb_proc = NFSPROC3_WRITE;
    //svc->rq_cksum = xid; /* i.e., not a real cksum */
    svc->rq_cksum = CityHash64((char *)&xid, sizeof(xid));

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

  bool verbose = true;
  static constexpr uint32_t item_wsize = 100;
  static constexpr uint32_t num_calls = 100;

//  NFSRequest** req_arr;
  uint32_t xid_ix;
  std::mutex mtx;
  std::condition_variable start_cond;

  class Worker
  {
  public:
    uint32_t thr_ix;
    NFSRequest** req_arr;
    struct timespec s_time;
    struct timespec e_time;

  Worker() = delete;

  Worker(uint32_t thr_ix)
    : thr_ix(thr_ix) {
      this->req_arr = new NFSRequest*[item_wsize];
      uint32_t xid_off = item_wsize * thr_ix;
      uint32_t xid_max = xid_ix + item_wsize;
      for (uint32_t ix = xid_off; ix < xid_max; ++ix) {
	this->req_arr[ix] = forge_v3_write("file1", ix, ix, 0);
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

      std::unique_lock<std::mutex> guard(mtx, std::defer_lock);
      start_cond.wait(guard);

      now(&s_time);

      for (uint32_t call_ctr = 0; call_ctr < item_wsize; ++call_ctr) {
	NFSRequest* cc_req = req_arr[call_ctr];
	if (verbose) {
	  std::cout
	    << " thread: " << thr_ix
	    << " call: " << call_ctr
	    << " NFSRequest: " << cc_req
	    << std::endl;
	}

	nfs_request_t* reqnfs = /* cc_req.get_nfs_req() */  &cc_req->req;
	struct svc_req* req = cc_req->get_svc_req();

	r = nfs_dupreq_start(reqnfs, req);
	r = nfs_dupreq_finish(req, NULL);
	nfs_dupreq_rele(req, NULL);
      }

      now(&e_time);
    } /* () */

    ~Worker() {
      for (uint32_t ix = 0; ix < item_wsize; ++ix) {
	delete this->req_arr[ix];
      }
      delete[] this->req_arr;
    }

  };

#if 0
  class DRCLatency1 : public ::testing::Test {

    virtual void SetUp() {

      xprt.xp_type = XPRT_TCP;

      struct sockaddr_in* sin = (struct sockaddr_in *) &xprt.xp_remote.ss;
      sin->sin_family = AF_INET;
      sin->sin_port = htons(remote_port);
      inet_pton(AF_INET, remote_addr, &sin->sin_addr);
      __rpc_address_setup(&xprt.xp_remote);

      /* setup reqs */
      req_arr = new NFSRequest*[item_wsize];
      for (uint32_t ix = 0; ix < item_wsize; ++ix) {
	req_arr[ix] = forge_v3_write("file1", ix, ix, 0);
      }

      /* setup TCP DRC */
      nfs_param.core_param.drc.disabled = false;
      nfs_param.core_param.drc.tcp.npart = DRC_TCP_NPART; // checked
      nfs_param.core_param.drc.tcp.size = DRC_TCP_SIZE; // checked
      nfs_param.core_param.drc.tcp.cachesz = 1; /* XXXX 0 crash; 1 harmless */
      nfs_param.core_param.drc.tcp.hiwat = 5; //DRC_TCP_HIWAT; // checked--even 364 negligible diff
      nfs_param.core_param.drc.tcp.recycle_npart = DRC_TCP_RECYCLE_NPART;
      nfs_param.core_param.drc.tcp.recycle_expire_s = 600;

      dupreq2_pkginit();

      if (verbose) {
	std::cout << "INIT"
		  << " insert next_xid: " << xid_ix
		  << std::endl;
      }
    }

    virtual void TearDown() {
      for (uint32_t ix = 0; ix < item_wsize; ++ix) {
	delete req_arr[ix];
      }
      delete[] req_arr; // XXX
    }

  };
#endif

  class DRCLatency2 : public ::testing::Test {
  public:
    std::vector<Worker*> workers;

    virtual void SetUp() {

      xprt.xp_type = XPRT_TCP;

      struct sockaddr_in* sin = (struct sockaddr_in *) &xprt.xp_remote.ss;
      sin->sin_family = AF_INET;
      sin->sin_port = htons(remote_port);
      inet_pton(AF_INET, remote_addr, &sin->sin_addr);
      __rpc_address_setup(&xprt.xp_remote);

      for (uint32_t ix = 0; ix < nthreads; ++ix) {
	auto worker = new Worker(ix);
	workers.push_back(worker);
      }

      /* setup TCP DRC */
      nfs_param.core_param.drc.disabled = false;
      nfs_param.core_param.drc.tcp.npart = DRC_TCP_NPART; // checked
      nfs_param.core_param.drc.tcp.size = DRC_TCP_SIZE; // checked
      nfs_param.core_param.drc.tcp.cachesz = 1; /* XXXX 0 crash; 1 harmless */
      nfs_param.core_param.drc.tcp.hiwat = 5; //DRC_TCP_HIWAT; // checked--even 364 negligible diff
      nfs_param.core_param.drc.tcp.recycle_npart = DRC_TCP_RECYCLE_NPART;
      nfs_param.core_param.drc.tcp.recycle_expire_s = 600;

      dupreq2_pkginit();
    }

    virtual void TearDown() {
      for (auto& worker : workers) {
	delete worker;
      }
    }

  };

} /* namespace */

#if 0
TEST_F(DRCLatency1, RUN1)
{
  struct timespec s_time, e_time;
  int r = 0;

  if (profile_out)
    ProfilerStart(profile_out);

  now(&s_time);

  for (uint32_t call_ctr = 0; call_ctr < item_wsize; ++call_ctr) {
    if (verbose) {
      std::cout
	<< " call: " << call_ctr
	<< std::endl;
    }

    NFSRequest& cc_req = *(req_arr[call_ctr]);
    nfs_request_t* reqnfs = /* cc_req.get_nfs_req() */  &cc_req.req;
    struct svc_req* req = cc_req.get_svc_req();

    r = nfs_dupreq_start(reqnfs, req);
    r = nfs_dupreq_finish(req, NULL);
    nfs_dupreq_rele(req, NULL);
  }

  now(&e_time);

  if (profile_out)
    ProfilerStop();

  fprintf(stderr, "total run time: %" PRIu64 " ns\n",
	  timespec_diff(&s_time, &e_time));
} /* TEST_F(DRCLatency1, RUN1) */
#endif

TEST_F(DRCLatency2, RUN1) {

  if (profile_out)
    ProfilerStart(profile_out);

  std::vector<std::thread> threads;
  threads.reserve(nthreads);
  for (auto& worker : workers) {
    threads.emplace_back(std::ref(*worker));
  }

  sleep(1);
  start_cond.notify_all();

  for (auto &t : threads)
    t.join();

  if (profile_out)
    ProfilerStop();

  /* total time, all threads */
  uint64_t total_time = 0;
  for (const auto& worker : workers) {
    total_time += timespec_diff(&worker->s_time, &worker->e_time);
  }

  fprintf(stderr, "total run time: %" PRIu64 " ns\n", total_time);

} /* TEST_F(DRCLatency2, RUN1) */

int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
