/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/* main.c
 * Module core functions
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include "nlm_list.h"
#include "FSAL/fsal_init.h"
#include "pseudofs_methods.h"

/* PSEUDOFS FSAL module private storage
 */

/* defined the set of attributes supported with POSIX */
#define PSEUDOFS_SUPPORTED_ATTRIBUTES (				\
	ATTR_TYPE     | ATTR_SIZE     |				\
	ATTR_FSID     | ATTR_FILEID   |				\
	ATTR_MODE     | ATTR_NUMLINKS | ATTR_OWNER     |	\
	ATTR_GROUP    | ATTR_ATIME    | ATTR_RAWDEV    |	\
	ATTR_CTIME    | ATTR_MTIME    | ATTR_SPACEUSED |	\
	ATTR_CHGTIME)

struct pseudo_fsal_module {
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;
	fsal_init_info_t fsal_info;
	/* pseudofsfs_specific_initinfo_t specific_info;  placeholder */
};

const char myname[] = "PSEUDOFS";

/* filesystem info for PSEUDOFS */
static struct fsal_staticfsinfo_t default_posix_info = {
	.maxfilesize = 0,
	.maxlink = 0,
	.maxnamelen = MAXNAMLEN,
	.maxpathlen = MAXPATHLEN,
	.no_trunc = true,
	.chown_restricted = true,
	.case_insensitive = false,
	.case_preserving = true,
	.link_support = false,
	.symlink_support = false,
	.lock_support = false,
	.lock_support_owner = false,
	.lock_support_async_block = false,
	.named_attr = false,
	.unique_handles = true,
	.lease_time = {10, 0},
	.acl_support = 0,
	.cansettime = true,
	.homogenous = true,
	.supported_attrs = PSEUDOFS_SUPPORTED_ATTRIBUTES,
	.maxread = 0,
	.maxwrite = 0,
	.umask = 0,
	.auth_exportpath_xdev = false,
	.xattr_access_rights = 0400,	/* root=RW, owner=R */
};

/* private helper for export object
 */

struct fsal_staticfsinfo_t *pseudofs_staticinfo(struct fsal_module *hdl)
{
	struct pseudo_fsal_module *myself;

	myself = container_of(hdl, struct pseudo_fsal_module, fsal);
	return &myself->fs_info;
}

/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t init_config(struct fsal_module *fsal_hdl,
				 config_file_t config_struct)
{
	struct pseudo_fsal_module *pseudofs_me =
	    container_of(fsal_hdl, struct pseudo_fsal_module, fsal);
	fsal_status_t fsal_status;

	/* get a copy of the defaults */
	pseudofs_me->fs_info = default_posix_info;

	fsal_status = fsal_load_config(fsal_hdl->ops->get_name(fsal_hdl),
				       config_struct,
				       &pseudofs_me->fsal_info,
				       &pseudofs_me->fs_info,
				       NULL);

	if (FSAL_IS_ERROR(fsal_status))
		return fsal_status;

	/* if we have fsal specific params, do them here
	 * fsal_hdl->name is used to find the block containing the
	 * params.
	 */

	display_fsinfo(&pseudofs_me->fs_info);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%" PRIx64,
		     (uint64_t) PSEUDOFS_SUPPORTED_ATTRIBUTES);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes default = 0x%" PRIx64,
		     default_posix_info.supported_attrs);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 pseudofs_me->fs_info.supported_attrs);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* my module private storage
 */

static struct pseudo_fsal_module PSEUDOFS;

/* linkage to the exports and handle ops initializers
 */

MODULE_INIT void pseudo_fsal_init(void)
{
	int retval;
	struct fsal_module *myself = &PSEUDOFS.fsal;

	retval =
	    register_fsal(myself, myname, FSAL_MAJOR_VERSION,
			  FSAL_MINOR_VERSION);
	if (retval != 0) {
		fprintf(stderr, "PSEUDOFS module failed to register");
		return;
	}
	myself->ops->create_export = pseudofs_create_export;
	myself->ops->init_config = init_config;
	init_fsal_parameters(&PSEUDOFS.fsal_info);
}

MODULE_FINI void pseudo_fsal_unload(void)
{
	int retval;

	retval = unregister_fsal(&PSEUDOFS.fsal);
	if (retval != 0) {
		fprintf(stderr, "PSEUDOFS module failed to unregister");
		return;
	}
}