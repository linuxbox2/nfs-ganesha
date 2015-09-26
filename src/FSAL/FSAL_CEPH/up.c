/*
 * Copyright © 2015, Red Hat, Inc.
 * Author: Matt Benjamin <mbenjamin@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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
 * @file   FSAL_CEPH/up.c
 * @author Matt Benjamin <mbenjamin@redhat.com>
 * @date   Fri Sep 25 18:07:01 2015
 *
 * @brief Upcalls
 *
 * Use new generic invalidate hook to drive upcalls.
 */

#include <fcntl.h>
#include <cephfs/libcephfs.h>
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_convert.h"
#include "fsal_api.h"
#include "internal.h"
#include "nfs_exports.h"
#include "FSAL/fsal_commonlib.h"

/**
 * @brief Invalidate an inode (dispatch upcall)
 *
 * This function terminates an invalidate upcall from libcephfs.  Since
 * upcalls are asynchronous, no upcall thread is required.
 *
 * @param[in] cmount The mount context
 * @param[in] ino The inode (number) being invalidated
 * @param[in] arg Opaque argument, currently a pointer to export
 *
 * @return FSAL status codes.
 */

int cephfsal_fs_invalidate(struct ceph_mount_info *cmount, vinodeno_t ino,
			void *arg)
{
	struct export *export = (struct export *) arg;
	const struct fsal_up_vector *up_ops;

	LogFullDebug(COMPONENT_FSAL_UP,
		"%s: invalidate on ino %" PRIu64 "\n", __func__,
		ino.ino.val);

	if (! export) {
		LogMajor(COMPONENT_FSAL_UP,
			"up/invalidate: called w/nil export");
		return EINVAL;
	}

	up_ops = export->export.up_ops;
	if (! up_ops) {
		LogMajor(COMPONENT_FSAL_UP,
			"up/invalidate: nil FSAL_UP ops vector");
		return EINVAL;
	}

	int rc;
	struct gsh_buffdesc fh_desc;

	fh_desc.addr = &ino;
	fh_desc.len = sizeof(vinodeno_t);

	uint32_t upflags =
		CACHE_INODE_INVALIDATE_ATTRS |
		CACHE_INODE_INVALIDATE_CONTENT;

	/* invalidate me, my man */
	rc = up_ops->invalidate(&CephFSM.fsal, &fh_desc, upflags);

	return rc;
}
