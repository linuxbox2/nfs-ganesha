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
#include <random>
#include "gtest/gtest.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/program_options.hpp>

extern "C" {

#include <intrinsic.h>
#include "nfs_dupreq.h"
#include "nfs_core.h"

extern const nfs_function_desc_t nfs3_func_desc[];


} /* extern "C" */

namespace {

  struct svc_xprt xprt;

  const char* remote_addr = "10.1.1.1";
  uint16_t remote_port = 45000;

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
    nfs_request_t* nfs = req->get_nfs_req();
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
  static constexpr uint32_t item_wsize = 10000;
  static constexpr uint32_t num_calls = 1000000;

  NFSRequest** req_arr;
  uint32_t xid_ix;

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

      /* setup DRC */
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

} /* namespace */

TEST_F(DRCLatency1, RUN1)
{
  int r = 0;
  for (uint32_t call_ctr = 0; call_ctr < item_wsize /* XXX */; ++call_ctr) {
    if (verbose) {
      std::cout
	<< " call: " << call_ctr
	<< std::endl;
    }
    NFSRequest& cc_req = *(req_arr[call_ctr]);
    nfs_request_t* reqnfs = cc_req.get_nfs_req();
    struct svc_req* req = cc_req.get_svc_req();

    r = nfs_dupreq_start(reqnfs, req);

  }
}

int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
