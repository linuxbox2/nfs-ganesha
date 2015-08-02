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
#include <random>
#include "gtest/gtest.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/exception.hpp>

extern "C" {
  /* Ganesha headers */
}

namespace bf = boost::filesystem;

namespace {

  bool create = false; // will pre-destroy
  bool destroy = false;

#if 0
  vnode_t* root_vnode = nullptr;
  inogen_t root_ino = {0, 0};
  creden_t acred = {0, 0};

  struct ZFSObject
  {
    std::string leaf_name;
    inogen_t ino;
    vnode_t* vnode;

  ZFSObject(std::string n) : leaf_name(std::move(n)), ino{0, 0},
      vnode(nullptr)
    {}
  };

  std::vector<ZFSObject> zfs1_objs;
  std::vector<ZFSObject> zfs2_objs;

  static constexpr int n_objs_c2 = 10000;

  std::uniform_int_distribution<uint8_t> uint_dist;
  std::mt19937 rng;

  p->cksum = XXH64(p->data, 65536, 8675309);

  ZFSObject c2_o(std::string("creates2"));
#endif
} /* namespace */

TEST(CI_HASH_DIST1, INIT)
{
  ASSERT_EQ(0, 0);
}


#if 0
TEST(ZFSIO, INIT)
{
  zhd = lzfw_init();
  ASSERT_NE(zhd, nullptr);

  ASSERT_EQ(is_directory(vdevs), true);  
  vdev1 /= "zd2";
}

TEST(ZFSIO, CREATEF1)
{
  int err, ix;
  zfs1_objs.reserve(100);
  for (ix = 0; ix < 100; ++ix) {
    unsigned o_flags;
    std::string n{"f" + std::to_string(ix)};
    zfs1_objs.emplace_back(ZFSObject(n));
    ZFSObject& o = zfs1_objs[ix];
    /* create and open */
    err = lzfw_openat(zhfs, &acred, root_vnode, o.leaf_name.c_str(),
		      O_RDWR|O_CREAT, 644, &o_flags, &o.vnode);
    ASSERT_EQ(err, 0);
  }
}

TEST(ZFSIO, CLOSE1)
{
  int err, ix;
  for (ix = 0; ix < 100; ++ix) {
    ZFSObject& o = zfs1_objs[ix];
    err = lzfw_close(zhfs, &acred, o.vnode, O_RDWR|O_CREAT);
    ASSERT_EQ(err, 0);
  }
}

TEST(ZFSIO, SHUTDOWN)
{
  int err;
  const char* lzw_err;

  // close root vnode
  err = lzfw_closedir(zhfs, &acred, root_vnode);
  root_vnode = nullptr;
  ASSERT_EQ(err, 0);

  // release fs
  err = lzfw_umount(zhfs, true /* force */);
  zhfs = nullptr;
  ASSERT_EQ(err, 0);

  // cond destroy everything
  if (destroy) {
    { // destroy fs
      char* fs = strdup("zp2/zf2");
      err = lzfw_dataset_destroy(zhd, fs, &lzw_err);
      ASSERT_EQ(err, 0);
      free(fs);
    }
    { // destroy pool
      err = lzfw_zpool_destroy(zhd, "zp2", true /* force */, &lzw_err);
      ASSERT_EQ(err, 0);
      remove(vdev1);
    }
  }

  // release library
  lzfw_exit(zhd);
  zhd = nullptr;
}
#endif

int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
