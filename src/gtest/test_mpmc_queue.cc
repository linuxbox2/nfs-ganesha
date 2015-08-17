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

#include <iostream>
#include <thread>
#include "gtest/gtest.h"

namespace {

  static constexpr int32_t COUNT = 100000000;

  template<typename T>
    long double run_test(T producer_func,
			 T consumer_func)
    {
      typedef std::chrono::high_resolution_clock clock_t;
      typedef std::chrono::time_point<clock_t> time_t;
      time_t start;
      time_t end;

      start = clock_t::now();
      std::thread producer(producer_func);
      std::thread consumer(consumer_func);

      producer.join();
      consumer.join();
      end = clock_t::now();

      return
        (end - start).count()
        * ((double) std::chrono::high_resolution_clock::period::num
           / std::chrono::high_resolution_clock::period::den);
    } /* run_test */

  /* Objects (pass by address) */
  struct Object {
  public:
    int ix;
  Object(int _ix) : ix(_ix) {}
  };

  std::vector<Object> ovec;

  extern "C" {
    typedef void mpmc_bounded_queue_t;
    extern void* make_mpmc_queue(size_t size);
    extern void destroy_mpmc_queue(void *q);
    extern bool call_mpmc_enqueue(void* q, void* o);
    extern bool call_mpmc_dequeue(void* q, void** o);
  }

  void obj_consumer_func(mpmc_bounded_queue_t *queue)
  {
    size_t count = COUNT;
    Object *obj = nullptr;
    
    while (count > 0) {
      if (call_mpmc_dequeue((void*) queue, (void**) &obj)) {
#if 0
	if (obj) {
	  std::cout << "dq: " << obj << " " << obj->ix << std::endl;
	}
#endif
	--count;
      }
    }
  }

  void obj_producer_func(mpmc_bounded_queue_t *queue)
  {
    size_t count = COUNT;
    while (count > 0) {
      Object* obj = &(ovec[count-1]);
#if 0
      std::cout << "nq: " << obj << " " << obj->ix << std::endl;
#endif
      if (call_mpmc_enqueue((void*) queue, (void*) obj)) {
	--count;
      }
    }
  }

} /* namespace */


TEST(QUEUES, MAKE_OBJECTS_1024)
{
  ovec.reserve(COUNT); /* yikes */
  for (int ix = 0; ix < COUNT; ++ix) {
    ovec.emplace_back(Object(ix));
  }
}

TEST(QUEUES, MPMC_OBJECT_1024)
{
  mpmc_bounded_queue_t *queue = make_mpmc_queue(1024);

  long double seconds =
    run_test(std::bind(&obj_producer_func, queue),
	     std::bind(&obj_consumer_func, queue));

  std::cout << "MPMC bound queue completed "
	    << COUNT
	    << " iterations in "
	    << seconds
	    << " seconds. "
	    << ((long double) COUNT / seconds) / 1000000
	    << " million enqueue/dequeue pairs per second."
	    << std::endl;

  destroy_mpmc_queue(queue);
}

TEST(QUEUES, OVCLEAR)
{
  ovec.clear();
}

int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
