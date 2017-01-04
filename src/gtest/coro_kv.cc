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
#include <boost/lexical_cast.hpp>
#include "gtest/gtest.h"

extern "C" {
  /* Ganesha headers */
}

namespace {
  using namespace std;

  string kprefix = "ganesha/node0/clientids";
  string k1 = "trmp1";
  string v1 = "orng badgermain no5";

  cmap_handle_t handle;
  cs_error_t err;
  int max_retries = 10;
  int how_many = 10000;

  bool verbose = false;
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

TEST(COROKV, INIT)
{
  handle = make_handle();
  ASSERT_EQ(0, 0);
}

TEST(COROKV, LIMITS1)
{
  if (verbose) {
    std::cout << "CMAP_KEYNAME_MAXLEN: " << CMAP_KEYNAME_MAXLEN << std::endl;
  }
  ASSERT_EQ(CMAP_KEYNAME_MAXLEN, 255); // SAD!
}

TEST(COROKV, SET1)
{
  string k = kprefix + k1;
  cs_error_t err = cmap_set_string(handle, k.c_str(), v1.c_str());
  ASSERT_EQ(err, CS_OK);
}

TEST(COROKV, GET1)
{
  string k = kprefix + k1;
  char *v;
  cs_error_t err = cmap_get_string(handle, k.c_str(), &v);
  ASSERT_EQ(err, CS_OK);
  if (verbose) {
    std::cout << __func__ << " k: " << k << " v: " << v << std::endl;
  }
}

TEST(COROKV, SETMANY1)
{
  /* set two key ranges */
  string prefix1 = kprefix + "/foo";
  string prefix2 = kprefix + "/bar";

  for (const auto& prefix : { prefix1, prefix2 }) {
    int ix;
    for (ix = 0; ix < how_many; ++ix) {
      string nk = prefix + "/k" + boost::lexical_cast<string>(ix);
      string nv = "value for " + nk;
      cs_error_t err = cmap_set_string(handle, nk.c_str(), nv.c_str());
      ASSERT_EQ(err, CS_OK);
    }
  }
}

TEST(COROKV, GETHALF1)
{
  /* find just keys whose prefix is prefix2 */
  string prefix2 = kprefix + "/bar";

  cs_error_t err;
  cmap_iter_handle_t iter_handle;
  char key_name[CMAP_KEYNAME_MAXLEN + 1];
  size_t value_len;
  cmap_value_types_t type;

  err = cmap_iter_init(handle, prefix2.c_str(), &iter_handle);
  ASSERT_EQ(err, CS_OK);

  if (verbose) {
    std::cout << "keys in prefix: " << prefix2 << std::endl;
  }
  while ((err = cmap_iter_next(handle, iter_handle, key_name, &value_len,
					    &type)) == CS_OK) {
    if (verbose) {
      std::cout << "\t" << key_name << std::endl;
    }
    ASSERT_EQ(type, CMAP_VALUETYPE_STRING);
  }

  cmap_iter_finalize(handle, iter_handle);
}

TEST(COROKV, DELETE_MANY1)
{
  cs_error_t err;
  cmap_iter_handle_t iter_handle;
  char key_name[CMAP_KEYNAME_MAXLEN + 1];
  size_t value_len;
  cmap_value_types_t type;
  int ndeleted{0};

  err = cmap_iter_init(handle, kprefix.c_str(), &iter_handle);
  ASSERT_EQ(err, CS_OK);

  while ((err = cmap_iter_next(handle, iter_handle, key_name, &value_len,
					    &type)) == CS_OK) {
    if (verbose) {
      std::cout << "deleting key:\t" << key_name << std::endl;
    }
    ASSERT_EQ(type, CMAP_VALUETYPE_STRING);

    /* nice, you can delete in an iteration */
    err = cmap_delete(handle, key_name);
    ASSERT_EQ(err, CS_OK);
    ++ndeleted;
  }

  cmap_iter_finalize(handle, iter_handle);
}

int main(int argc, char *argv[])
{
  namespace po = boost::program_options;

  po::options_description desc("Options");
  desc.add_options()
    ("usage", "How to use this")
    ("verbose", "print stuff")
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

    if (vm.count("verbose")) verbose = true;
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
