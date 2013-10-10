/*
 * Copyright (C) 2012, Linux Box Corporation
 * All Rights Reserved
 *
 * Contributor: Matt Benjamin
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

/**
 *
 * \file extent.h
 * \author Matt Benjamin
 * \brief File mapping support
 *
 * \section DESCRIPTION
 *
 * File mapping support.
 *
 */

#ifndef VFS_EXTENT_H
#define VFS_EXTENT_H

#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <misc/rbtree.h>
#include "fsal.h"
#include "abstract_mem.h"

#define VFS_MAPPING_FLAGS_NONE   0x0000
#define VFS_MAPPING_FLAGS_DIRTY  0x0001

#define VFS_FILE_MAP_NONE        0x0000

#define VFS_MAP_SIZE   4194304 * 8 /* 32M */
#define VFS_MAP_SHIFT  22 /* XXX needed? */
#define VFS_MAP_PROT   (PROT_READ|PROT_WRITE)
#define VFS_MAP_FLAGS  MAP_SHARED /* in future, might want MAP_HUGETLB */


struct mapping
{
    struct opr_rbtree_node node_k;
    pthread_spinlock_t sp;
    uint64_t off;
    uint32_t len; /* fixed for any set of mappings */
    void *addr;
    uint32_t flags;
    uint32_t refcnt;
};

extern pool_t *extent_pool;
extern pool_t *uio_pool;

/*
 * Well-ordering function.
 *
 * If extents were of variable size, we could define
 * equivalence as intersection, since two such extent sequences
 * would conflict.  Then A < B, iff A->start < B->off and also
 * A->off < B->off+B->len.  A > B is the converse.
 *
 * However, it is even simpler to require mappings to be of fixed
 * size, since mappings also may be large.  Then mappings may be
 * ordered on offset.
 */
static inline int
vfs_fsal_mapping_cmpf(const struct opr_rbtree_node *lhs,
                      const struct opr_rbtree_node *rhs)
{
    struct mapping *lk, *rk;

    lk = opr_containerof(lhs, struct mapping, node_k);
    rk = opr_containerof(rhs, struct mapping, node_k);

    if (lk->off < rk->off)
        return (-1);

    if (lk->off == rk->off)
        return (0);

    return (1);
}

void vfs_fsal_extent_init(void);

/* extent base address */
#define vfs_extent_of(offset) ((offset) & ~(VFS_MAP_SIZE - 1))

/* next offset (offset is aligned) */
#define vfs_extent_next(offset) ((offset) + VFS_MAP_SIZE)

#define vfs_extents_in_range(offset, length)  (((length) / VFS_MAP_SIZE) + 1)

#endif /* VFS_EXTENT_H */
