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
#include <iostream>
#include <vector>
#include <map>
#include <thread>
#include <random>
#include "gtest/gtest.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/exception.hpp>

extern "C" {
/* Ganesha headers */
#include "nfs_lib.h"
#include "export_mgr.h"
#include "cache_inode.h"
}

namespace bf = boost::filesystem;

namespace {

  const uint16_t export_id = 77; // parameterize
  struct gsh_export* a_export = nullptr;

#if 0
  std::uniform_int_distribution<uint8_t> uint_dist;
  std::mt19937 rng;

  p->cksum = XXH64(p->data, 65536, 8675309);
#endif

  int ganesha_server() {
    /* XXX */
    return nfs_libmain();
  }

} /* namespace */

TEST(CI_HASH_DIST1, INIT)
{
  a_export = get_gsh_export(export_id);
  ASSERT_EQ(0, 0);
}

int main(int argc, char *argv[])
{
  int code;
  ::testing::InitGoogleTest(&argc, argv);
  std::thread ganesha(ganesha_server);
  code  = RUN_ALL_TESTS();
  ganesha.join();

  return code;
}
