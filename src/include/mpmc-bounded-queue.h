// This is free and unencumbered software released into the public domain.

// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.

// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// For more information, please refer to <http://unlicense.org/>

// Implementation of Dmitry Vyukov's MPMC algorithm
// http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue


#ifndef __MPMC_BOUNDED_QUEUE_INCLUDED__
#define __MPMC_BOUNDED_QUEUE_INCLUDED__

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdalign.h>
#include <assert.h>

/* XXX 128-bit cache lines are out there */
typedef char cache_line_pad_t[64]; // it's either 32 or 64 so 64 is good enough

typedef struct mpmc_node_t
{
	void *data;
        atomic_size_t seq;
} mpmc_node_t;

typedef struct mpmc_bounded_queue_t
{
	cache_line_pad_t    _pad0;
	size_t              _size;
	size_t              _mask;
	mpmc_node_t *       _buffer;
	cache_line_pad_t    _pad1;
	atomic_size_t       _head_seq;
	cache_line_pad_t    _pad2;
	atomic_size_t       _tail_seq;
	cache_line_pad_t    _pad3;
} mpmc_bounded_queue_t;

static inline void init_mpmc_queue(mpmc_bounded_queue_t *q, size_t size) {

	/* make sure it's a power of 2 */
        assert((size != 0) && ((size & (~size + 1)) == size));
	
        q->_size = size;
        q->_mask = size - 1;
        q->_buffer = (mpmc_node_t*) aligned_alloc(alignof(mpmc_node_t),
						(size * sizeof(mpmc_node_t)));
        q->_head_seq = 0;
        q->_tail_seq = 0;
	
        /* populate the sequence initial values */
        for (size_t i = 0; i < size; ++i) {
		//_buffer[i].seq.store(i, std::memory_order_relaxed);
		atomic_store_explicit(
			&(q->_buffer[i].seq), i, memory_order_relaxed);
        }
}

static inline void finalize_mpmc_queue(mpmc_bounded_queue_t *q) {
	free(q->_buffer);
}


static inline bool mpmc_enqueue(mpmc_bounded_queue_t *q, void *data) {
        /* _head_seq only wraps at MAX(_head_seq) instead we use a mask to
	 * convert the sequence to an array index
	 * 
	 * this is why the ring buffer must be a size which is a power of 2.
	 * this also allows the sequence to double as a ticket/lock
	 */
        size_t head_seq =
		atomic_load_explicit(&(q->_head_seq), memory_order_relaxed);

        for (;;) {
		mpmc_node_t *node = &(q->_buffer[head_seq & q->_mask]);
		size_t node_seq =
			atomic_load_explicit(&(node->seq),
					memory_order_acquire);
		intptr_t dif = (intptr_t) node_seq - (intptr_t) head_seq;

		/* if seq and head_seq are the same then it means this slot is
		 * empty */
		if (dif == 0) {
			/* claim our spot by moving head
			 * if head isn't the same as we last checked then that
			 * means someone beat us to the punch
			 * weak compare is faster, but can return spurious
			 * results, which in this instance is OK, because it's
			 * in the loop
			 */
			bool r =
				atomic_compare_exchange_weak_explicit(
					&(q->_head_seq),
					&head_seq,
					head_seq + 1,
					memory_order_relaxed,
					memory_order_relaxed);
			if (r) {
				// set the data
				node->data = data;
				/* increment the sequence so that the tail
				 * knows it's accessible */
				atomic_store_explicit(&(node->seq),
						head_seq + 1,
						memory_order_release);
				return true;
			}
		}
		else if (dif < 0) {
			/* if seq is less than head seq then it means this slot
			 * is full and therefore the buffer is full */
			return false;
		}
		else {
			/* under normal circumstances this branch should never
			 * be taken */
			head_seq =
				atomic_load_explicit(&(q->_head_seq),
						memory_order_relaxed);
		}
        }

        // never taken
        return false;
} /* mpmc_enqueue */

static inline bool mpmc_dequeue(mpmc_bounded_queue_t *q, void **data) {
        size_t tail_seq =
		atomic_load_explicit(&(q->_tail_seq), memory_order_relaxed);

        for (;;) {
		mpmc_node_t *node = &(q->_buffer[tail_seq & q->_mask]);
		size_t node_seq =
			atomic_load_explicit(&(node->seq),
					memory_order_acquire);
		intptr_t dif = (intptr_t) node_seq - (intptr_t)(tail_seq + 1);

		/* if seq and head_seq are the same then it means this slot is
		 * empty */
		if (dif == 0) {
			/* claim our spot by moving head
			 * if head isn't the same as we last checked then that
			 * means someone beat us to the punch
			 * weak compare is faster, but can return spurious
			 * results, which in this instance is OK, because it's
			 * in the loop */
			bool r =
				atomic_compare_exchange_weak_explicit(
					&(q->_tail_seq),
					&tail_seq,
					tail_seq + 1,
					memory_order_relaxed,
					memory_order_relaxed);
			if (r) {
				/* set the output */
				*data = node->data;
				/* set the sequence to what the head sequence
				 * should be next time around */
				atomic_store_explicit(&(node->seq),
						tail_seq + q->_mask + 1,
						memory_order_release);
				return true;
			}
		}
		else if (dif < 0) {
			/* if seq is less than head seq then it means this slot
			 * is full and therefore the buffer is full */
			return false;
		}
		else {
			/* under normal circumstances this branch should never
			 * be taken */
			tail_seq =
				atomic_load_explicit(&(q->_tail_seq),
						memory_order_relaxed);
		}
        }
        /* never taken */
        return false;
}

#endif /* __MPMC_BOUNDED_QUEUE_INCLUDED__ */
