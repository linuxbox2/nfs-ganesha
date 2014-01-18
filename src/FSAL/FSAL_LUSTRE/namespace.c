/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/* namespace.c
 * LUSTRE FSAL namespace object
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <mntent.h>
#include <sys/statvfs.h>
#include "nlm_list.h"
#include "fsal_handle.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "fsal_handle.h"
#include "lustre_methods.h"

#ifdef HAVE_INCLUDE_LUSTREAPI_H
#include <lustre/lustreapi.h>
#include <lustre/lustre_user.h>
#else
#ifdef HAVE_INCLUDE_LIBLUSTREAPI_H
#include <lustre/liblustreapi.h>
#include <lustre/lustre_user.h>
#include <linux/quota.h>
#endif
#endif

/*
 * LUSTRE internal namespace
 */

struct lustre_fsal_namespace {
	struct fsal_namespace namespace;
	char *mntdir;
	char *fs_spec;
	char *fstype;
	int root_fd;
	dev_t root_dev;
	struct file_handle *root_handle;
	bool  pnfs_enabled;
};

char *lustre_get_root_path(struct fsal_namespace *namespace)
{
	struct lustre_fsal_namespace *myself;

	myself = container_of(namespace, struct lustre_fsal_namespace,
			      namespace);
	return myself->mntdir;
}

/* helpers to/from other VFS objects
 */

struct fsal_staticfsinfo_t *lustre_staticinfo(struct fsal_module *hdl);

int lustre_get_root_fd(struct fsal_namespace *namespace)
{
	struct lustre_fsal_namespace *myself;

	myself = container_of(namespace, struct lustre_fsal_namespace,
			      namespace);
	return myself->root_fd;
}

/* namespace object methods
 */

static fsal_status_t lustre_release(struct fsal_namespace *namespace)
{
	struct lustre_fsal_namespace *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(namespace, struct lustre_fsal_namespace,
			      namespace);

	pthread_mutex_lock(&namespace->lock);
	if (namespace->refs > 0 || !glist_empty(&namespace->handles)) {
		LogMajor(COMPONENT_FSAL, "namespace (0x%p)busy",
			 namespace);
		fsal_error = posix2fsal_error(EBUSY);
		retval = EBUSY;
		goto errout;
	}
	fsal_detach_namespace(namespace->fsal, &namespace->namespaces);
	free_namespace_ops(namespace);
	if (myself->root_fd >= 0)
		close(myself->root_fd);
	if (myself->root_handle != NULL)
		gsh_free(myself->root_handle);
	if (myself->fstype != NULL)
		gsh_free(myself->fstype);
	if (myself->mntdir != NULL)
		gsh_free(myself->mntdir);
	if (myself->fs_spec != NULL)
		gsh_free(myself->fs_spec);
	myself->namespace.ops = NULL;	/* poison myself */
	pthread_mutex_unlock(&namespace->lock);

	pthread_mutex_destroy(&namespace->lock);
	gsh_free(myself);		/* elvis has left the building */
	return fsalstat(fsal_error, retval);

 errout:
	pthread_mutex_unlock(&namespace->lock);
	return fsalstat(fsal_error, retval);
}

static fsal_status_t lustre_get_dynamic_info(struct fsal_namespace *namespace,
					     const struct req_op_context *opctx,
					     fsal_dynamicfsinfo_t *infop)
{
	struct lustre_fsal_namespace *myself;
	struct statvfs buffstatvfs;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	if (!infop) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself = container_of(namespace, struct lustre_fsal_namespace,
			      namespace);
	retval = fstatvfs(myself->root_fd, &buffstatvfs);
	if (retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	infop->total_bytes = buffstatvfs.f_frsize * buffstatvfs.f_blocks;
	infop->free_bytes = buffstatvfs.f_frsize * buffstatvfs.f_bfree;
	infop->avail_bytes = buffstatvfs.f_frsize * buffstatvfs.f_bavail;
	infop->total_files = buffstatvfs.f_files;
	infop->free_files = buffstatvfs.f_ffree;
	infop->avail_files = buffstatvfs.f_favail;
	infop->time_delta.tv_sec = 1;
	infop->time_delta.tv_nsec = 0;

 out:
	return fsalstat(fsal_error, retval);
}

static bool lustre_fs_supports(struct fsal_namespace *namespace,
			       fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(namespace->fsal);
	return fsal_supports(info, option);
}

static uint64_t lustre_fs_maxfilesize(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(namespace->fsal);
	return fsal_maxfilesize(info);
}

static uint32_t lustre_fs_maxread(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(namespace->fsal);
	return fsal_maxread(info);
}

static uint32_t lustre_fs_maxwrite(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(namespace->fsal);
	return fsal_maxwrite(info);
}

static uint32_t lustre_fs_maxlink(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(namespace->fsal);
	return fsal_maxlink(info);
}

static uint32_t lustre_fs_maxnamelen(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(namespace->fsal);
	return fsal_maxnamelen(info);
}

static uint32_t lustre_fs_maxpathlen(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(namespace->fsal);
	return fsal_maxpathlen(info);
}

static struct timespec lustre_fs_lease_time(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(namespace->fsal);
	return fsal_lease_time(info);
}

static fsal_aclsupp_t lustre_fs_acl_support(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(namespace->fsal);
	return fsal_acl_support(info);
}

static attrmask_t lustre_fs_supported_attrs(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(namespace->fsal);
	return fsal_supported_attrs(info);
}

static uint32_t lustre_fs_umask(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(namespace->fsal);
	return fsal_umask(info);
}

static uint32_t lustre_fs_xattr_access_rights(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = lustre_staticinfo(namespace->fsal);
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

static fsal_status_t lustre_get_quota(struct fsal_namespace *namespace,
				      const char *filepath,
				      int quota_type,
				      struct req_op_context *req_ctx,
				      fsal_quota_t *pquota)
{
	struct lustre_fsal_namespace *myself;
	struct if_quotactl dataquota;

	struct stat path_stat;
	uid_t id;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;

	myself = container_of(namespace, struct lustre_fsal_namespace,
			      namespace);
	retval = stat(filepath, &path_stat);
	if (retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "VFS get_quota, fstat: root_path: %s, fd=%d, errno=(%d) %s",
			 myself->mntdir, myself->root_fd, errno,
			 strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	if (path_stat.st_dev != myself->root_dev) {
		LogMajor(COMPONENT_FSAL,
			 "VFS get_quota: crossed mount boundary! root_path: %s, quota path: %s",
			 myself->mntdir, filepath);
		fsal_error = ERR_FSAL_FAULT;	/* maybe a better error? */
		retval = 0;
		goto out;
	}
	memset((char *)&dataquota, 0, sizeof(struct if_quotactl));

	dataquota.qc_cmd = LUSTRE_Q_GETQUOTA;
	dataquota.qc_type = quota_type;
	dataquota.qc_id = id =
	    (quota_type ==
	     USRQUOTA) ? req_ctx->creds->caller_uid : req_ctx->creds->
	    caller_gid;

	retval = llapi_quotactl((char *)filepath, &dataquota);

	if (retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	pquota->bsize = 1024;	/* LUSTRE has block of 1024 bytes */
	pquota->bhardlimit = dataquota.qc_dqblk.dqb_bhardlimit;
	pquota->bsoftlimit = dataquota.qc_dqblk.dqb_bsoftlimit;
	pquota->curblocks = dataquota.qc_dqblk.dqb_curspace / pquota->bsize;

	pquota->fhardlimit = dataquota.qc_dqblk.dqb_ihardlimit;
	pquota->fsoftlimit = dataquota.qc_dqblk.dqb_isoftlimit;
	pquota->curfiles = dataquota.qc_dqblk.dqb_isoftlimit;

	/* Times left are set only if used resource
	 * is in-between soft and hard limits */
	if ((pquota->curfiles > pquota->fsoftlimit)
	    && (pquota->curfiles < pquota->fhardlimit))
		pquota->ftimeleft = dataquota.qc_dqblk.dqb_itime;
	else
		pquota->ftimeleft = 0;

	if ((pquota->curblocks > pquota->bsoftlimit)
	    && (pquota->curblocks < pquota->bhardlimit))
		pquota->btimeleft = dataquota.qc_dqblk.dqb_btime;
	else
		pquota->btimeleft = 0;

 out:
	return fsalstat(fsal_error, retval);
}

/* set_quota
 * same lower mount restriction applies
 */

static fsal_status_t lustre_set_quota(struct fsal_namespace *namespace,
			       const char *filepath,
			       int quota_type,
			       struct req_op_context *req_ctx,
			       fsal_quota_t *pquota, fsal_quota_t *presquota)
{
	struct lustre_fsal_namespace *myself;
	struct stat path_stat;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval;
	struct if_quotactl dataquota;

	myself = container_of(namespace, struct lustre_fsal_namespace,
			      namespace);
	retval = stat(filepath, &path_stat);
	if (retval < 0) {
		LogMajor(COMPONENT_FSAL,
			 "VFS set_quota, fstat: root_path: %s, fd=%d, errno=(%d) %s",
			 myself->mntdir, myself->root_fd, errno,
			 strerror(errno));
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto err;
	}
	if (path_stat.st_dev != myself->root_dev) {
		LogMajor(COMPONENT_FSAL,
			 "VFS set_quota: crossed mount boundary! root_path: %s, quota path: %s",
			 myself->mntdir, filepath);
		fsal_error = ERR_FSAL_FAULT;	/* maybe a better error? */
		retval = 0;
		goto err;
	}

	memset((char *)&dataquota, 0, sizeof(struct if_quotactl));
	dataquota.qc_cmd = LUSTRE_Q_GETQUOTA;
	dataquota.qc_type = quota_type;
	dataquota.qc_id =
	    (quota_type ==
	     USRQUOTA) ? req_ctx->creds->caller_uid : req_ctx->creds->
	    caller_gid;

	/* Convert FSAL structure to FS one */
	if (pquota->bhardlimit != 0) {
		dataquota.qc_dqblk.dqb_bhardlimit = pquota->bhardlimit;
		dataquota.qc_dqblk.dqb_valid |= QIF_BLIMITS;
	}

	if (pquota->bsoftlimit != 0) {
		dataquota.qc_dqblk.dqb_bsoftlimit = pquota->bsoftlimit;
		dataquota.qc_dqblk.dqb_valid |= QIF_BLIMITS;
	}

	if (pquota->fhardlimit != 0) {
		dataquota.qc_dqblk.dqb_ihardlimit = pquota->fhardlimit;
		dataquota.qc_dqblk.dqb_valid |= QIF_ILIMITS;
	}

	if (pquota->btimeleft != 0) {
		dataquota.qc_dqblk.dqb_btime = pquota->btimeleft;
		dataquota.qc_dqblk.dqb_valid |= QIF_BTIME;
	}

	if (pquota->ftimeleft != 0) {
		dataquota.qc_dqblk.dqb_itime = pquota->ftimeleft;
		dataquota.qc_dqblk.dqb_valid |= QIF_ITIME;
	}

	retval = llapi_quotactl((char *)filepath, &dataquota);

	if (retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto err;
	}
	if (presquota != NULL)
		return lustre_get_quota(namespace, filepath, quota_type,
				 req_ctx, presquota);
 err:
	return fsalstat(fsal_error, retval);
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.  There
 * is the option to also adjust the start pointer.
 */

static fsal_status_t lustre_extract_handle(struct fsal_namespace *namespace,
				    fsal_digesttype_t in_type,
				    struct gsh_buffdesc *fh_desc)
{
	struct lustre_file_handle *hdl;
	size_t fh_size;

	/* sanity checks */
	if (!fh_desc || !fh_desc->addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	hdl = (struct lustre_file_handle *)fh_desc->addr;
	fh_size = lustre_sizeof_handle(hdl);
	if (in_type != FSAL_DIGEST_SIZEOF && fh_desc->len != fh_size) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be %lu, got %lu",
			 fh_size, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	fh_desc->len = fh_size;	/* pass back the actual size */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Create a FSAL data server handle from a wire handle
 *
 * This function creates a FSAL data server handle from a client
 * supplied "wire" handle.  This is also where validation gets done,
 * since PUTFH is the only operation that can return
 * NFS4ERR_BADHANDLE.
 *
 * @param[in]  namespace  The namespace in which to create the handle
 * @param[in]  desc       Buffer from which to create the file
 * @param[out] ds_pub     FSAL data server handle
 *
 * @return NFSv4.1 error codes.
 */
nfsstat4 lustre_create_ds_handle(struct fsal_namespace *const namespace,
				 const struct gsh_buffdesc *const desc,
				 struct fsal_ds_handle **const ds_pub)
{
	/* Handle to be created */
	struct lustre_ds *ds = NULL;

	*ds_pub = NULL;

	if (desc->len != sizeof(struct lustre_file_handle))
		return NFS4ERR_BADHANDLE;
	ds = gsh_calloc(1, sizeof(struct lustre_ds));

	if (ds == NULL)
		return NFS4ERR_SERVERFAULT;

	/* Connect lazily when a FILE_SYNC4 write forces us to, not
	 *            here. */

	ds->connected = false;

	memcpy(&ds->wire, desc->addr, desc->len);

	if (fsal_ds_handle_init(&ds->ds,
				namespace->ds_ops,
				namespace)) {
		gsh_free(ds);
		return NFS4ERR_SERVERFAULT;
	}
	*ds_pub = &ds->ds;

	return NFS4_OK;
}

/* lustre_namespace_ops_init
 * overwrite vector entries with the methods that we support
 */

void lustre_namespace_ops_init(struct namespace_ops *ops)
{
	ops->release = lustre_release;
	ops->lookup_path = lustre_lookup_path;
	ops->extract_handle = lustre_extract_handle;
	ops->create_handle = lustre_create_handle;
	ops->create_ds_handle = lustre_create_ds_handle;
	ops->get_fs_dynamic_info = lustre_get_dynamic_info;
	ops->fs_supports = lustre_fs_supports;
	ops->fs_maxfilesize = lustre_fs_maxfilesize;
	ops->fs_maxread = lustre_fs_maxread;
	ops->fs_maxwrite = lustre_fs_maxwrite;
	ops->fs_maxlink = lustre_fs_maxlink;
	ops->fs_maxnamelen = lustre_fs_maxnamelen;
	ops->fs_maxpathlen = lustre_fs_maxpathlen;
	ops->fs_lease_time = lustre_fs_lease_time;
	ops->fs_acl_support = lustre_fs_acl_support;
	ops->fs_supported_attrs = lustre_fs_supported_attrs;
	ops->fs_umask = lustre_fs_umask;
	ops->fs_xattr_access_rights = lustre_fs_xattr_access_rights;
	ops->get_quota = lustre_get_quota;
	ops->set_quota = lustre_set_quota;
}

/* create_export
 * Create an namespace point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the namespace and then put the fsal back.
 * returns the namespace with one reference taken.
 */

fsal_status_t lustre_create_export(struct fsal_module *fsal_hdl,
				   const char *export_path,
				   const char *fs_options,
				   struct exportlist *exp_entry,
				   struct fsal_module *next_fsal,
				   const struct fsal_up_vector *up_ops,
				   struct fsal_namespace **namespace)
{
	struct lustre_fsal_namespace *myself;
	FILE *fp;
	struct mntent *p_mnt;
	size_t pathlen, outlen = 0;
	char mntdir[MAXPATHLEN];
	char fs_spec[MAXPATHLEN];
	char type[MAXNAMLEN];
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	*namespace = NULL;		/* poison it first */
	if (export_path == NULL || strlen(export_path) == 0
	    || strlen(export_path) > MAXPATHLEN) {
		LogMajor(COMPONENT_FSAL,
			 "export path empty or too big");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}
	if (next_fsal != NULL) {
		LogCrit(COMPONENT_FSAL, "This module is not stackable");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	myself = gsh_malloc(sizeof(struct lustre_fsal_namespace));
	if (myself == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "out of memory for object");
		return fsalstat(posix2fsal_error(errno), errno);
	}
	memset(myself, 0, sizeof(struct lustre_fsal_namespace));
	myself->root_fd = -1;

	retval = fsal_namespace_init(&myself->namespace, exp_entry);
	if (retval != 0)
		goto errout;

	lustre_namespace_ops_init(myself->namespace.ops);
	lustre_handle_ops_init(myself->namespace.obj_ops);
	myself->namespace.up_ops = up_ops;

	/* lock myself before attaching to the fsal.
	 * keep myself locked until done with creating myself.
	 */

	pthread_mutex_lock(&myself->namespace.lock);
	retval = fsal_attach_namespace(fsal_hdl, &myself->namespace.namespaces);
	if (retval != 0)
		goto errout;	/* seriously bad */
	myself->namespace.fsal = fsal_hdl;

	/* start looking for the mount point */
	fp = setmntent(MOUNTED, "r");
	if (fp == NULL) {
		retval = errno;
		LogCrit(COMPONENT_FSAL, "Error %d in setmntent(%s): %s", retval,
			MOUNTED, strerror(retval));
		fsal_error = posix2fsal_error(retval);
		goto errout;
	}
	while ((p_mnt = getmntent(fp)) != NULL) {
		if (p_mnt->mnt_dir != NULL) {
			pathlen = strlen(p_mnt->mnt_dir);
			if (pathlen > outlen) {
				if (strcmp(p_mnt->mnt_dir, "/") == 0) {
					outlen = pathlen;
					strncpy(mntdir, p_mnt->mnt_dir,
						MAXPATHLEN);
					strncpy(type, p_mnt->mnt_type,
						MAXNAMLEN);
					strncpy(fs_spec, p_mnt->mnt_fsname,
						MAXPATHLEN);
				} else
				    if ((strncmp
					 (export_path, p_mnt->mnt_dir,
					  pathlen) == 0)
					&& ((export_path[pathlen] == '/')
					    || (export_path[pathlen] ==
						'\0'))) {
					if (strcasecmp
					    (p_mnt->mnt_type, "lustre") != 0) {
						LogDebug(COMPONENT_FSAL,
							 "Mount (%s) is not LUSTRE, skipping",
							 p_mnt->mnt_dir);
						continue;
					}
					outlen = pathlen;
					strncpy(mntdir, p_mnt->mnt_dir,
						MAXPATHLEN);
					strncpy(type, p_mnt->mnt_type,
						MAXNAMLEN);
					strncpy(fs_spec, p_mnt->mnt_fsname,
						MAXPATHLEN);
				}
			}
		}
	}
	endmntent(fp);
	if (outlen <= 0) {
		LogCrit(COMPONENT_FSAL, "No mount entry matches '%s' in %s",
			export_path, MOUNTED);
		fsal_error = ERR_FSAL_NOENT;
		goto errout;
	}
	myself->root_fd = open(mntdir, O_RDONLY | O_DIRECTORY);
	if (myself->root_fd < 0) {
		LogMajor(COMPONENT_FSAL,
			 "Could not open VFS mount point %s: rc = %d", mntdir,
			 errno);
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto errout;
	} else {
		struct stat root_stat;
		struct lustre_file_handle *fh =
		    alloca(sizeof(struct lustre_file_handle));

		memset(fh, 0, sizeof(struct lustre_file_handle));
		retval = fstat(myself->root_fd, &root_stat);
		if (retval < 0) {
			LogMajor(COMPONENT_FSAL,
				 "fstat: root_path: %s, fd=%d, errno=(%d) %s",
				 mntdir, myself->root_fd, errno,
				 strerror(errno));
			fsal_error = posix2fsal_error(errno);
			retval = errno;
			goto errout;
		}
		myself->root_dev = root_stat.st_dev;
		retval = lustre_path_to_handle(export_path, fh);
		if (retval < 0) {
			LogMajor(COMPONENT_FSAL,
				 "lustre_name_to_handle_at: root_path: %s, root_fd=%d, errno=(%d) %s",
				 mntdir, myself->root_fd, errno,
				 strerror(errno));
			fsal_error = posix2fsal_error(errno);

			if (errno == ENOTTY)
				LogFatal(COMPONENT_FSAL,
					 "Critical error in FSAL, exiting... Check if %s is mounted",
					 mntdir);

			retval = errno;
			goto errout;
		}
		myself->root_handle
			= gsh_malloc(sizeof(struct lustre_file_handle));
		if (myself->root_handle == NULL) {
			LogMajor(COMPONENT_FSAL,
				 "memory for root handle, errno=(%d) %s", errno,
				 strerror(errno));
			fsal_error = posix2fsal_error(errno);
			retval = errno;
			goto errout;
		}
		memcpy(myself->root_handle, fh,
		       sizeof(struct lustre_file_handle));
	}
	myself->fstype = gsh_strdup(type);
	myself->fs_spec = gsh_strdup(fs_spec);
	myself->mntdir = gsh_strdup(mntdir);
	*namespace = &myself->namespace;
	pthread_mutex_unlock(&myself->namespace.lock);

	LogInfo(COMPONENT_FSAL,
		"pnfs was enabled for [%s]",
		export_path);
	namespace_ops_pnfs(myself->namespace.ops);
	handle_ops_pnfs(myself->namespace.obj_ops);
	ds_ops_init(myself->namespace.ds_ops);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:
	if (myself->root_fd >= 0)
		close(myself->root_fd);
	if (myself->root_handle != NULL)
		gsh_free(myself->root_handle);
	if (myself->fstype != NULL)
		gsh_free(myself->fstype);
	if (myself->mntdir != NULL)
		gsh_free(myself->mntdir);
	if (myself->fs_spec != NULL)
		gsh_free(myself->fs_spec);
	myself->namespace.ops = NULL;	/* poison myself */
	pthread_mutex_unlock(&myself->namespace.lock);
	pthread_mutex_destroy(&myself->namespace.lock);
	gsh_free(myself);		/* elvis has left the building */
	return fsalstat(fsal_error, retval);
}
