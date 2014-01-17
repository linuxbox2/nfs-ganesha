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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* namespace.c
 * PSEUDO FSAL namespace object
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <os/mntent.h>
#include <os/quota.h>
#include <dlfcn.h>
#include "nlm_list.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "fsal_handle_syscalls.h"
#include "pseudofs_methods.h"

/* helpers to/from other PSEUDO objects
 */

struct fsal_staticfsinfo_t *pseudofs_staticinfo(struct fsal_module *hdl);

/* namespace object methods
 */

static fsal_status_t release(struct fsal_namespace *namespace)
{
	struct pseudofs_fsal_namespace *myself;

	myself =
	     container_of(namespace, struct pseudofs_fsal_namespace, namespace);

	pthread_mutex_lock(&namespace->lock);

	if (namespace->refs > 0 || !glist_empty(&namespace->handles)) {
		LogMajor(COMPONENT_FSAL,
			 "namespace %p - %s busy",
			 namespace, myself->export_path);
		pthread_mutex_unlock(&namespace->lock);
		return fsalstat(posix2fsal_error(EBUSY), EBUSY);
	}

	fsal_detach_namespace(namespace->fsal, &namespace->namespaces);
	free_namespace_ops(namespace);

	pthread_mutex_unlock(&namespace->lock);

	pthread_mutex_destroy(&namespace->lock);

	if (myself->export_path != NULL)
		gsh_free(myself->export_path);

	gsh_free(myself);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t get_dynamic_info(struct fsal_namespace *namespace,
				      const struct req_op_context *opctx,
				      fsal_dynamicfsinfo_t *infop)
{
	infop->total_bytes = 0;
	infop->free_bytes = 0;
	infop->avail_bytes = 0;
	infop->total_files = 0;
	infop->free_files = 0;
	infop->avail_files = 0;
	infop->time_delta.tv_sec = 1;
	infop->time_delta.tv_nsec = 0;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static bool fs_supports(struct fsal_namespace *namespace,
			fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(namespace->fsal);
	return fsal_supports(info, option);
}

static uint64_t fs_maxfilesize(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(namespace->fsal);
	return fsal_maxfilesize(info);
}

static uint32_t fs_maxread(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(namespace->fsal);
	return fsal_maxread(info);
}

static uint32_t fs_maxwrite(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(namespace->fsal);
	return fsal_maxwrite(info);
}

static uint32_t fs_maxlink(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(namespace->fsal);
	return fsal_maxlink(info);
}

static uint32_t fs_maxnamelen(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(namespace->fsal);
	return fsal_maxnamelen(info);
}

static uint32_t fs_maxpathlen(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(namespace->fsal);
	return fsal_maxpathlen(info);
}

static struct timespec fs_lease_time(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(namespace->fsal);
	return fsal_lease_time(info);
}

static fsal_aclsupp_t fs_acl_support(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(namespace->fsal);
	return fsal_acl_support(info);
}

static attrmask_t fs_supported_attrs(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(namespace->fsal);
	return fsal_supported_attrs(info);
}

static uint32_t fs_umask(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(namespace->fsal);
	return fsal_umask(info);
}

static uint32_t fs_xattr_access_rights(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(namespace->fsal);
	return fsal_xattr_access_rights(info);
}

/* get_quota
 * return quotas for this namespace.
 * path could cross a lower mount boundary which could
 * mask lower mount values with those of the namespace root
 * if this is a real issue, we can scan each time with setmntent()
 * better yet, compare st_dev of the file with st_dev of root_fd.
 * on linux, can map st_dev -> /proc/partitions name -> /dev/<name>
 */

static fsal_status_t get_quota(struct fsal_namespace *namespace,
			       const char *filepath, int quota_type,
			       struct req_op_context *req_ctx,
			       fsal_quota_t *pquota)
{
	/* PSEUDOFS doesn't support quotas */
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* set_quota
 * same lower mount restriction applies
 */

static fsal_status_t set_quota(struct fsal_namespace *namespace,
			       const char *filepath, int quota_type,
			       struct req_op_context *req_ctx,
			       fsal_quota_t *pquota, fsal_quota_t *presquota)
{
	/* PSEUDOFS doesn't support quotas */
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.  There
 * is the option to also adjust the start pointer.
 *
 * So, adjust the start pointer, check.  But setting the length
 * to sizeof(vfs_file_handle_t) coerces all handles to a value
 * too large for some applications (e.g., ESXi), and much larger
 * than necessary.  (On my Linux system, I'm seeing 12 byte file
 * handles (EXT4).  Since this routine has no idea what the
 * internal length was, it should not set the value (the length
 * comes from us anyway, it's up to us to get it right elsewhere).
 */

static fsal_status_t extract_handle(struct fsal_namespace *namespace,
				    fsal_digesttype_t in_type,
				    struct gsh_buffdesc *fh_desc)
{
	size_t fh_min;

	fh_min = 1;

	if (in_type == FSAL_DIGEST_NFSV2) {
		if (fh_desc->len < fh_min) {
			LogMajor(COMPONENT_FSAL,
				 "V2 size too small for handle.  should be >= %lu, got %lu",
				 fh_min, fh_desc->len);
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
		}
	} else if (in_type != FSAL_DIGEST_SIZEOF && fh_desc->len < fh_min) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be >= %lu, got %lu",
			 fh_min, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* pseudofs_namespace_ops_init
 * overwrite vector entries with the methods that we support
 */

void pseudofs_namespace_ops_init(struct namespace_ops *ops)
{
	ops->release = release;
	ops->lookup_path = pseudofs_lookup_path;
	ops->extract_handle = extract_handle;
	ops->create_handle = pseudofs_create_handle;
	ops->get_fs_dynamic_info = get_dynamic_info;
	ops->fs_supports = fs_supports;
	ops->fs_maxfilesize = fs_maxfilesize;
	ops->fs_maxread = fs_maxread;
	ops->fs_maxwrite = fs_maxwrite;
	ops->fs_maxlink = fs_maxlink;
	ops->fs_maxnamelen = fs_maxnamelen;
	ops->fs_maxpathlen = fs_maxpathlen;
	ops->fs_lease_time = fs_lease_time;
	ops->fs_acl_support = fs_acl_support;
	ops->fs_supported_attrs = fs_supported_attrs;
	ops->fs_umask = fs_umask;
	ops->fs_xattr_access_rights = fs_xattr_access_rights;
	ops->get_quota = get_quota;
	ops->set_quota = set_quota;
}

/* create_export
 * Create an namespace point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the namespace and then put the fsal back.
 * returns the namespace with one reference taken.
 */

fsal_status_t pseudofs_create_export(struct fsal_module *fsal_hdl,
				     const char *export_path,
				     const char *fs_specific,
				     struct exportlist *exp_entry,
				     struct fsal_module *next_fsal,
				     const struct fsal_up_vector *up_ops,
				     struct fsal_namespace **namespace)
{
	struct pseudofs_fsal_namespace *myself;
	int retval = 0;

	*namespace = NULL;		/* poison it first */

	if (next_fsal != NULL) {
		LogCrit(COMPONENT_FSAL, "This module is not stackable");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	myself = gsh_calloc(1, sizeof(struct pseudofs_fsal_namespace));

	if (myself == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "Could not allocate namespace");
		return fsalstat(posix2fsal_error(errno), errno);
	}

	retval = fsal_namespace_init(&myself->namespace, exp_entry);

	if (retval != 0) {
		LogMajor(COMPONENT_FSAL,
			 "Could not initialize namespace");
		gsh_free(myself);
		return fsalstat(posix2fsal_error(retval), retval);
	}

	pseudofs_namespace_ops_init(myself->namespace.ops);
	pseudofs_handle_ops_init(myself->namespace.obj_ops);

	myself->namespace.up_ops = up_ops;

	/* lock myself before attaching to the fsal.
	 * keep myself locked until done with creating myself.
	 */

	pthread_mutex_lock(&myself->namespace.lock);

	retval = fsal_attach_namespace(fsal_hdl, &myself->namespace.namespaces);

	if (retval != 0) {
		/* seriously bad */
		LogMajor(COMPONENT_FSAL,
			 "Could not attach namespace");
		goto errout;
	}

	myself->namespace.fsal = fsal_hdl;

	/* Save the export path. */
	myself->export_path = gsh_strdup(export_path);

	if (myself->export_path == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Could not allocate export path");
		retval = ENOMEM;
		goto errout;
	}

	*namespace = &myself->namespace;

	pthread_mutex_unlock(&myself->namespace.lock);

	LogDebug(COMPONENT_FSAL,
		 "Created namespace %p - %s",
		 myself, myself->export_path);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:

	if (myself->export_path != NULL)
		gsh_free(myself->export_path);

	if (myself->root_handle != NULL)
		gsh_free(myself->root_handle);

	free_namespace_ops(&myself->namespace);

	pthread_mutex_unlock(&myself->namespace.lock);
	pthread_mutex_destroy(&myself->namespace.lock);

	gsh_free(myself);	/* elvis has left the building */

	return fsalstat(posix2fsal_error(retval), retval);
}
