/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2013, The Linux Box Corporation
 * Contributor : Matt Benjamin <matt@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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

#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include "nlm_list.h"
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "cache_inode.h"
#include "cache_inode_hash.h"

/**
 * @addtogroup cache_inode
 * @{
 */

/**
 *
 * @file cache_inode_hash.c
 * @author Matt Benjamin
 * @brief Cache inode hashed dictionary package
 *
 * @section Description
 *
 * This module exports an interface for efficient lookup of cache entries
 * by file handle.  Refactored from the prior abstract HashTable
 * implementation.
 */

struct cih_lookup_table *cih_fhcache_temp; /* XXX going away */
static bool initialized;

static pthread_rwlockattr_t rwlock_attr;

/**
 * @brief Initialize the package.
 */
void
cih_pkginit(void)
{
	uint32_t cache_sz = 32767;	/* XXX */

	/* avoid writer starvation */
	pthread_rwlockattr_init(&rwlock_attr);
#ifdef GLIBC
	pthread_rwlockattr_setkind_np(
		&rwlock_attr,
		PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif
	cih_fhcache_temp = cih_alloc_fhcache(nfs_param.cache_param.nparts,
					     cache_sz);
	initialized = true;
}

/**
 * @brief Create a new cache inode hash table
 *
 * This function creates a new cache inode hash table, taking as
 * parameters a number of partitions and number of cache slotes (which
 * should be prime).
 *
 * @param npart [in] Number of partitions
 * @param cache_sz [in] Number of cache slots
 *
 * @return The table.
 */
struct cih_lookup_table *
cih_alloc_fhcache(uint32_t npart, uint32_t cache_sz)
{
	int ix;
	cih_partition_t *cp;
	struct cih_lookup_table *cih_fhcache =
		gsh_calloc(1, sizeof(struct cih_lookup_table));

	cih_fhcache->npart = npart;
	cih_fhcache->partition = gsh_calloc(npart, sizeof(cih_partition_t));
	for (ix = 0; ix < npart; ++ix) {
		cp = &cih_fhcache->partition[ix];
		cp->part_ix = ix;
		pthread_rwlock_init(&cp->lock, &rwlock_attr);
		avltree_init(&cp->t, cih_fh_cmpf, 0 /* must be 0 */);
		cih_fhcache->cache_sz = cache_sz;
		cp->cache = gsh_calloc(cache_sz, sizeof(struct avltree_node *));
	}

	return (cih_fhcache);
}

/** @} */
