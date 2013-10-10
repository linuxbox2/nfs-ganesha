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

#include <assert.h>
#include "extent.h"

pool_t *extent_pool;
pool_t *uio_pool; /* XXX move */

void vfs_fsal_extent_init(void)
{
    extent_pool = pool_init("VFS FSAL Extent Pool",
                            sizeof(struct mapping),
                            pool_basic_substrate,
                            NULL, NULL, NULL);

    uio_pool = pool_init("VFS FSAL UIO Pool",
                         sizeof(struct gsh_uio),
                         pool_basic_substrate,
                         NULL, NULL, NULL);
}

#include "extent_inline.h"

int vfs_extent_prune_extents(struct vfs_fsal_obj_handle *hdl)
{
    struct opr_rbtree_node *node;
    struct mapping *map;
    int retval = 0;

    pthread_spin_lock(&hdl->maps.sp);
    if (opr_rbtree_size(&hdl->maps.t) > 0 ) {
        while ((node = opr_rbtree_first(&hdl->maps.t)) != NULL) {
            map = opr_containerof(node, struct mapping, node_k);
            pthread_spin_lock(&map->sp);
            --(map->refcnt);
            if (map->refcnt == 0) {
                retval = vfs_extent_remove_mapping(hdl, map);
                continue;
            }
            pthread_spin_unlock(&map->sp);
        }
    }
    pthread_spin_unlock(&hdl->maps.sp);

    return (retval);
}
