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
#include <chrono>
#include <thread>
#include <random>
#include "gtest/gtest.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/program_options.hpp>

extern "C" {
/* Ganesha headers */
#include "nfs_lib.h"
#include "export_mgr.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "nfs_exports.h"
#include "fsal.h"
}

using namespace std;
using namespace std::literals;

namespace po = boost::program_options;
namespace bf = boost::filesystem;

namespace {

  char* ganesha_conf = nullptr;
  char* lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;
  cache_inode_fsal_data_t fsdata;

  uint32_t n_objects = 250*1000;

  bool create_objects = false;
  bool delete_test_root = false;

  struct req_op_context req_ctx;
  struct user_cred user_credentials;

  struct gsh_export* a_export = nullptr;

  const char* root_entry_name = "ci_hash_dist1";
  cache_entry_t* root_entry = nullptr;
  cache_entry_t* test_root = nullptr;

  struct TFile {
    std::string leaf_name;
    cache_entry_t* entry[3];

  TFile(std::string n) : leaf_name(std::move(n))
      {
	memset(entry, 0, 3*sizeof(cache_entry_t*));
      }
  };

  std::vector<TFile> test_objs;

#if 0
  std::uniform_int_distribution<uint8_t> uint_dist;
  std::mt19937 rng;

  p->cksum = XXH64(p->data, 65536, 8675309);
#endif

  int ganesha_server() {
    /* XXX */
    return nfs_libmain(
      ganesha_conf,
      lpath,
      dlevel
      );
  }

} /* namespace */

TEST(CI_HASH_DIST1, INIT)
{
  cache_inode_status_t status;

  a_export = get_gsh_export(export_id);
  ASSERT_NE(a_export, nullptr);

  status = nfs_export_get_root_entry(a_export, &root_entry);
  ASSERT_EQ(status, CACHE_INODE_SUCCESS);
  ASSERT_NE(root_entry, nullptr);

  /* callers of cache_inode_get need fsdata */
  fsdata.cifd_export = a_export->fsal_export;

  /* Ganesha call paths need real or forged context info */
  memset(&user_credentials, 0, sizeof(struct user_cred));
  memset(&req_ctx, 0, sizeof(struct req_op_context));

  req_ctx.ctx_export = a_export;
  req_ctx.fsal_export = a_export->fsal_export;
  req_ctx.creds = &user_credentials;

  /* stashed in tls */
  op_ctx = &req_ctx;
}

TEST(CI_HASH_DIST1, CREATE_ROOT)
{
  cache_inode_status_t status;

  // use the existing paths if the root exists
  status = cache_inode_lookup(root_entry, root_entry_name, &test_root);
  if (status == CACHE_INODE_SUCCESS) {
    cout << "Reusing existing test root (" << root_entry_name << ")"  << endl;
    ASSERT_NE(test_root, nullptr);
    return;
  }

  cout << "Creating new test root (" << root_entry_name << ")"  << endl;
  create_objects = true;

  // create root directory for test
  status = cache_inode_create(root_entry, root_entry_name,
			      DIRECTORY, 777, NULL /* create arg */,
			      &test_root);
  ASSERT_EQ(status, CACHE_INODE_SUCCESS);
  ASSERT_NE(test_root, nullptr);
}

TEST(CI_HASH_DIST1, LOOKUP_OR_CREATEF1)
{
  cache_inode_status_t status;

  if(create_objects) {
    // create them
    test_objs.reserve(n_objects);
    for (int ix = 0; ix < n_objects; ++ix) {
      string n{"f" + std::to_string(ix)};
      test_objs.emplace_back(TFile(n));
      TFile& o = test_objs[ix];
      status = cache_inode_create(test_root, o.leaf_name.c_str(),
				  REGULAR_FILE, 644, NULL /* create arg */,
				  &o.entry[0]);
      ASSERT_EQ(status, CACHE_INODE_SUCCESS);
      ASSERT_NE(o.entry[0], nullptr);
    }
  } else {
    // lookup instead
    test_objs.reserve(n_objects);
    for (int ix = 0; ix < n_objects; ++ix) {
      string n{"f" + std::to_string(ix)};
      test_objs.emplace_back(TFile(n));
      TFile& o = test_objs[ix];
      status = cache_inode_lookup(test_root, o.leaf_name.c_str(), &o.entry[0]);
      ASSERT_EQ(status, CACHE_INODE_SUCCESS);
      ASSERT_NE(o.entry[0], nullptr);
    }
  }
}

TEST(CI_HASH_DIST1, WAIT1)
{
  cout << "Thread in WAIT1" << endl;
  std::this_thread::sleep_for(5s);
}

TEST(CI_HASH_DIST1, EX_REF1)
{
  cache_inode_status_t status;
  for (int ix = 0; ix < n_objects; ++ix) {
    TFile& o = test_objs[ix];
    ASSERT_NE(o.entry[0], nullptr);
    status = cache_inode_lru_ref(o.entry[0], LRU_FLAG_NONE);
    ASSERT_EQ(status, CACHE_INODE_SUCCESS);
  }
}

TEST(CI_HASH_DIST1, EX_UNREF1)
{
  for (int ix = 0; ix < n_objects; ++ix) {
    TFile& o = test_objs[ix];
    cache_inode_lru_unref(o.entry[0], LRU_FLAG_NONE);
  }
}

TEST(CI_HASH_DIST1, GET_IE_INITIAL_REF1)
{
  cache_inode_status_t status;
  for (int ix = 0; ix < n_objects; ++ix) {
    TFile& o = test_objs[ix];
    /* shallow copy entry's est. key */
    fsdata.fh_desc = o.entry[0]->fh_hk.key.kv;
    status = cache_inode_get(&fsdata, &o.entry[1]);
    ASSERT_EQ(status, CACHE_INODE_SUCCESS);
    /* we could test entry[0] and entry[1] for eq, if no eviction */
    ASSERT_NE(o.entry[1], nullptr);
  }
}

TEST(CI_HASH_DIST1, INITAL_UNREF1)
{
  for (int ix = 0; ix < n_objects; ++ix) {
    TFile& o = test_objs[ix];
    cache_inode_lru_unref(o.entry[1], LRU_FLAG_NONE);
  }
}

/* XXX check dist */

TEST(CI_HASH_DIST1, REMOVE1)
{
  cache_inode_status_t status;

  /* preserve test root? */
  if (! delete_test_root)
    return;

  for (int ix = 0; ix < n_objects; ++ix) {
    TFile& o = test_objs[ix];
    status = cache_inode_remove(test_root, o.leaf_name.c_str());
    ASSERT_EQ(status, CACHE_INODE_SUCCESS);
    /* put last ref */
    cache_inode_put(o.entry[0]); /* XXX no status */
  }
}

TEST(CI_HASH_DIST1, REMOVE_ROOT)
{
  cache_inode_status_t status;

  /* preserve test root? */
  if (! delete_test_root)
    return;

  status = cache_inode_remove(root_entry, root_entry_name);
  ASSERT_EQ(status, CACHE_INODE_SUCCESS);
  /* put last ref */
  cache_inode_put(test_root); /* XXX no status */
  test_root = nullptr;
}

int main(int argc, char *argv[])
{
  int code = 0;

  po::options_description opts("program options");
  po::variables_map vm;

  try {

    opts.add_options()
      ("config", po::value<string>(),
	"path to Ganesha conf file")

      ("logfile", po::value<string>(),
	"log to the provided file path")

      ("export", po::value<uint16_t>(),
	"id of export on which to operate (must exist)")

      ("nobjects", po::value<uint32_t>(),
	"count of file objects to create (single dir, default 250K)")

      ("delete", "delete objects at end of test")

      ("debug", po::value<string>(),
	"ganesha debug level")
      ;

    po::variables_map::iterator vm_iter;
    po::store(po::parse_command_line(argc, argv, opts), vm);
    po::notify(vm);

    // use config vars--leaves them on the stack
    vm_iter = vm.find("config");
    if (vm_iter != vm.end()) {
      ganesha_conf = (char*) vm_iter->second.as<std::string>().c_str();
    }
    vm_iter = vm.find("logfile");
    if (vm_iter != vm.end()) {
      lpath = (char*) vm_iter->second.as<std::string>().c_str();
    }
    vm_iter = vm.find("export");
    if (vm_iter != vm.end()) {
      export_id = vm_iter->second.as<uint16_t>();
    }
    vm_iter = vm.find("nobjects");
    if (vm_iter != vm.end()) {
      n_objects = vm_iter->second.as<uint32_t>();
    }
    vm_iter = vm.find("delete");
    if (vm_iter != vm.end()) {
      delete_test_root = true;
    }
    vm_iter = vm.find("debug");
    if (vm_iter != vm.end()) {
      dlevel = ReturnLevelAscii(
	(char*) vm_iter->second.as<std::string>().c_str());
    }

    cout << "Starting " << argv[0]
	 << " with " << n_objects << " objects" << endl;

    ::testing::InitGoogleTest(&argc, argv);

    std::thread ganesha(ganesha_server);
    cout << "In WAIT for ganesha startup" << endl;
    std::this_thread::sleep_for(5s);
    cout << "Start" << endl;

    code  = RUN_ALL_TESTS();
    ganesha.join();
  }

  catch(po::error& e) {
    cout << "Error parsing opts " << e.what() << endl;
  }

  catch(...) {
    cout << "Unhandled exception in main()" << endl;
  }

  return code;
}
