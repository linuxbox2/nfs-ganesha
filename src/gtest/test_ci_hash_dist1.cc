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
}

namespace bf = boost::filesystem;

namespace {

  struct gsh_export* a_export = nullptr;
  char* ganesha_conf = nullptr;
  char* lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;

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
  a_export = get_gsh_export(export_id);
  ASSERT_EQ(0, 0);
}

int main(int argc, char *argv[])
{
  int code = 0;

  using namespace std;
  using namespace std::literals;
  namespace po = boost::program_options;

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
    vm_iter = vm.find("debug");
    if (vm_iter != vm.end()) {
      dlevel = ReturnLevelAscii(
	(char*) vm_iter->second.as<std::string>().c_str());
    }
    vm_iter = vm.find("export");
    if (vm_iter != vm.end()) {
      export_id = vm_iter->second.as<uint16_t>();
    }

    ::testing::InitGoogleTest(&argc, argv);

    std::thread ganesha(ganesha_server);
    std::this_thread::sleep_for(5s);

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
