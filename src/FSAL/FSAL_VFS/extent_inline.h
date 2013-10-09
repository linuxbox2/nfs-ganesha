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
 * \file extent_inline.h
 * \author Matt Benjamin
 * \brief File mapping support
 *
 * \section DESCRIPTION
 *
 * File mapping support.
 *
 */

#ifndef VFS_EXTENT_INLINE_H
#define VFS_EXTENT_INLINE_H

#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <misc/rbtree.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "vfs_methods.h"
#include "abstract_mem.h"

/* called with:  hdl->maps.mtx, map->sp LOCKED */
static inline int
vfs_extent_remove_mapping(struct vfs_fsal_obj_handle *hdl,
                          struct mapping *map)
{
    int retval;

    opr_rbtree_remove(&hdl->maps.t, &map->node_k);
    /* unmap it */
    retval = munmap(map->addr, VFS_MAP_SIZE);
    pthread_mutex_unlock(&hdl->maps.mtx);
    pthread_spin_unlock(&map->sp);
    pthread_spin_destroy(&map->sp);
    pool_free(extent_pool, map);

    return (retval);
}

int vfs_extent_prune_extents(struct vfs_fsal_obj_handle *hdl);

#endif /* VFS_EXTENT_INLINE_H */
