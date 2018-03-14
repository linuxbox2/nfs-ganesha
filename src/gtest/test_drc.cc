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
#include <iostream>
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


} /* extern "C" */

namespace {

  nfs_request_t* req_arr;

  bool verbose = false;
  static constexpr uint32_t item_wsize = 10000;
  static constexpr uint32_t num_calls = 1000000;

  uint32_t xid_ix;

  class DRCLatency1 : public ::testing::Test {

    virtual void SetUp() {

      if (verbose) {
	std::cout << "INIT"
		  << " insert next_xid: " << xid_ix
		  << std::endl;
      }
    }

    virtual void TearDown() {
    }

  };

} /* namespace */

TEST_F(DRCLatency1, RUN1)
{
  for (uint32_t call_ctr = 0; call_ctr < num_calls; ++call_ctr) {
    if (verbose) {
      std::cout
	<< " call: " << call_ctr
	<< std::endl;
    }
  }
}

int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
