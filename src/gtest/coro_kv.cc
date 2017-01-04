// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (C) 2015 Red Hat, Inc.
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
#include <corosync/corotypes.h>
#include <corosync/cmap.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <random>
#include <boost/program_options.hpp>
#include "gtest/gtest.h"

extern "C" {
  /* Ganesha headers */
}

namespace {
  using namespace std;

  string kprefix = "ganesha/node0/clientids/";
  string k1 = "trmp1";
  string v1 = "orng badgermain no5";

  cmap_handle_t handle;
  cs_error_t err;
  int max_retries = 10;

  bool verbose = true;
  bool do_set = false;
  bool do_get = true;
  bool do_getkeys = true;
  bool do_rm = false;
} /* namespace */

cmap_handle_t make_handle(void) {
  cmap_handle_t handle;
  int retries = 0;
  while ((err = cmap_initialize(&handle)) == CS_ERR_TRY_AGAIN &&
	retries++ < max_retries) {
    sleep(1);
  }
  return handle;
}

TEST(EXAMPLE, INIT)
{
  handle = make_handle();
  ASSERT_EQ(0, 0);
}

TEST(EXAMPLE, SET1)
{
  string k = kprefix + k1;
  cs_error_t err = cmap_set_string(handle, k.c_str(), v1.c_str());
  ASSERT_EQ(err, CS_OK);
}

TEST(EXAMPLE, GET1)
{
  string k = kprefix + k1;
  char *v;
  cs_error_t err = cmap_get_string(handle, k.c_str(), &v);
  ASSERT_EQ(err, CS_OK);
  if (verbose) {
    std::cout << __func__ << " k: " << k << " v: " << v << std::endl;
  }
}

int main(int argc, char *argv[])
{
  namespace po = boost::program_options;

  po::options_description desc("Options");
  desc.add_options()
    ("usage", "How to use this")
    ("set", "set key(s)")
    ("get", "get value(s) by key(s)")
    ("getkeys", "list key(s) by prefix")
    ("rm", "remove named keys");

  po::variables_map vm;

  try {
    po::store(po::parse_command_line(argc, argv, desc),
	      vm);

    if (vm.count("usage")) {
      std::cout << "Usage: " << "(stuff)" << std::endl;
      return 1;
    }

    if (vm.count("set")) do_set = true;
    if (vm.count("get")) do_get = true;
    if (vm.count("getkeys")) do_getkeys = true;
    if (vm.count("rm")) do_rm = true;

    po::notify(vm);
  }
  catch(po::error& e) {
    std::cerr << "error parsing program options: " << e.what()
	      << std::endl;
      return 2;
  }

  /* do it */
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
