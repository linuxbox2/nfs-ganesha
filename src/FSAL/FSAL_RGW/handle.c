/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Red Hat Inc., 2015
 * Author: Orit Wasserman <owasserm@redhat.com>
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

/* handle.c
 * RGW object (file|dir) handle object
 */

#include <fcntl.h>
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_convert.h"
#include "fsal_api.h"
#include "internal.h"
#include "nfs_exports.h"
#include "FSAL/fsal_commonlib.h"

/**
 * @brief Release an object
 *
 * This function looks up an object by name in a directory.
 *
 * @param[in] obj_hdl The object to release
 *
 * @return FSAL status codes.
 */

static void release(struct fsal_obj_handle *obj_hdl)
{
	/* The private 'full' handle */
	struct rgw_handle *obj =
		container_of(obj_hdl, struct rgw_handle, handle);
	struct rgw_export *export = obj->export;

	if (obj->rgw_fh != export->rgw_fs->root_fh) {
		/* release RGW ref */
		(void) rgw_fh_rele(export->rgw_fs, obj->rgw_fh,
				0 /* flags */);

		/* fsal API */
		fsal_obj_handle_fini(&obj->handle);
		gsh_free(obj);
	}
}

/**
 * @brief Look up an object by name
 *
 * This function looks up an object by name in a directory.
 *
 * @param[in]     dir_hdl    The directory in which to look up the object.
 * @param[in]     path       The name to look up.
 * @param[in,out] obj_hdl    The looked up object.
 * @param[in,out] attrs_out  Optional attributes for newly created object
 *
 * @return FSAL status codes.
 */
static fsal_status_t lookup(struct fsal_obj_handle *dir_hdl,
			const char *path, struct fsal_obj_handle **obj_hdl,
			struct attrlist *attrs_out)
{
	/* Generic status return */
	int rc = 0;
	/* Stat output */
	struct stat st;
	/* The private 'full' export */
	struct rgw_export *export =
		container_of(op_ctx->fsal_export, struct rgw_export, export);
	struct rgw_handle *dir = container_of(dir_hdl, struct rgw_handle,
					handle);
	struct rgw_handle *obj = NULL;

	/* rgw file handle */
	struct rgw_file_handle *rgw_fh;

	/* XXX presently, we can only fake attrs--maybe rgw_lookup should
	 * take struct stat pointer OUT as libcephfs' does */
	rc = rgw_lookup(export->rgw_fs, dir->rgw_fh, path, &rgw_fh,
			RGW_LOOKUP_FLAG_NONE);
	if (rc < 0)
		return rgw2fsal_error(rc);

	rc = rgw_getattr(export->rgw_fs, rgw_fh, &st, RGW_GETATTR_FLAG_NONE);
	if (rc < 0)
		return rgw2fsal_error(rc);

	rc = construct_handle(export, rgw_fh, &st, &obj);
	if (rc < 0) {
		return rgw2fsal_error(rc);
	}

	*obj_hdl = &obj->handle;

	if (attrs_out != NULL) {
		posix2fsal_attributes(&st, attrs_out);

		/* Make sure ATTR_RDATTR_ERR is cleared on success. */
		attrs_out->mask &= ~ATTR_RDATTR_ERR;
	}


	return fsalstat(0, 0);
}

struct rgw_cb_arg {
	fsal_readdir_cb cb;
	void *fsal_arg;
	struct fsal_obj_handle *dir_hdl;
	attrmask_t attrmask;
};

static bool rgw_cb(const char *name, void *arg, uint64_t offset)
{
	struct rgw_cb_arg *rgw_cb_arg = arg;
	struct fsal_obj_handle *obj;
	fsal_status_t status;
	struct attrlist attrs;
	bool cb_rc;

	fsal_prepare_attrs(&attrs, rgw_cb_arg->attrmask);

	status = lookup(rgw_cb_arg->dir_hdl, name, &obj, &attrs);
	if (FSAL_IS_ERROR(status))
		return false;

	cb_rc = rgw_cb_arg->cb(name, obj, &attrs, rgw_cb_arg->fsal_arg, offset);

	fsal_release_attrs(&attrs);

	return cb_rc;
}

/**
 * @brief Read a directory
 *
 * This function reads the contents of a directory (excluding . and
 * .., which is ironic since the Ceph readdir call synthesizes them
 * out of nothing) and passes dirent information to the supplied
 * callback.
 *
 * @param[in]  dir_hdl     The directory to read
 * @param[in]  whence      The cookie indicating resumption, NULL to start
 * @param[in]  dir_state   Opaque, passed to cb
 * @param[in]  cb          Callback that receives directory entries
 * @param[out] eof         True if there are no more entries
 *
 * @return FSAL status.
 */

static fsal_status_t rgw_fsal_readdir(struct fsal_obj_handle *dir_hdl,
				  fsal_cookie_t *whence, void *cb_arg,
				  fsal_readdir_cb cb, attrmask_t attrmask,
				  bool *eof)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
		container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' directory handle */
	struct rgw_handle *dir = container_of(dir_hdl, struct rgw_handle,
					handle);
	struct rgw_cb_arg rgw_cb_arg = {cb, cb_arg, dir_hdl, attrmask};
	/* Return status */
	fsal_status_t fsal_status = {ERR_FSAL_NO_ERROR, 0};

	uint64_t r_whence = (whence) ? *whence : 0;
	rc = rgw_readdir(export->rgw_fs, dir->rgw_fh, &r_whence, rgw_cb,
			&rgw_cb_arg, eof, RGW_READDIR_FLAG_NONE);
	if (rc < 0)
		return rgw2fsal_error(rc);

	return fsal_status;
}

/**
 * @brief Create a regular file
 *
 * This function creates an empty, regular file.
 *
 * @param[in]     dir_hdl    Directory in which to create the file
 * @param[in]     name       Name of file to create
 * @param[in]     attrs_in   Attributes of newly created file
 * @param[in,out] obj_hdl    Handle for newly created file
 * @param[in,out] attrs_out  Optional attributes for newly created object
 *
 * @return FSAL status.
 */

static fsal_status_t rgw_fsal_create(struct fsal_obj_handle *dir_hdl,
				const char *name,
				struct attrlist *attrs_in,
				struct fsal_obj_handle **obj_hdl,
				struct attrlist *attrs_out)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' directory handle */
	struct rgw_handle *dir = container_of(dir_hdl, struct rgw_handle,
					      handle);
	/* New file handle */
	struct rgw_file_handle *rgw_fh;
	/* Status after create */
	struct stat st;
	/* Newly created object */
	struct rgw_handle *obj;

	memset(&st, 0, sizeof(struct stat));

	st.st_uid = op_ctx->creds->caller_uid;
	st.st_gid = op_ctx->creds->caller_gid;
	st.st_mode = fsal2unix_mode(attrs_in->mode)
	    & ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	uint32_t create_mask =
		RGW_SETATTR_UID | RGW_SETATTR_GID | RGW_SETATTR_MODE;

	rc = rgw_create(export->rgw_fs, dir->rgw_fh, name, &st, create_mask,
			&rgw_fh, 0 /* posix flags */, RGW_CREATE_FLAG_NONE);
	if (rc < 0)
		return rgw2fsal_error(rc);

	rc = construct_handle(export, rgw_fh, &st, &obj);
	if (rc < 0) {
		return rgw2fsal_error(rc);
	}

	*obj_hdl = &obj->handle;

	if (attrs_out != NULL) {
		posix2fsal_attributes(&st, attrs_out);

		/* Make sure ATTR_RDATTR_ERR is cleared on success. */
		attrs_out->mask &= ~ATTR_RDATTR_ERR;
	}


	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Create a directory
 *
 * This funcion creates a new directory.
 *
 * @param[in]     dir_hdl    The parent in which to create
 * @param[in]     name       Name of the directory to create
 * @param[in]     attrs_in   Attributes of the newly created directory
 * @param[in,out] obj_hdl    Handle of the newly created directory
 * @param[in,out] attrs_out  Optional attributes for newly created object
 *
 * @return FSAL status.
 */

static fsal_status_t rgw_fsal_mkdir(struct fsal_obj_handle *dir_hdl,
				const char *name, struct attrlist *attrs_in,
				struct fsal_obj_handle **obj_hdl,
				struct attrlist *attrs_out)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' directory handle */
	struct rgw_handle *dir = container_of(dir_hdl, struct rgw_handle,
					      handle);
	/* New file handle */
	struct rgw_file_handle *rgw_fh;
	/* Stat result */
	struct stat st;
	/* Newly created object */
	struct rgw_handle *obj = NULL;

	memset(&st, 0, sizeof(struct stat));

	st.st_uid = op_ctx->creds->caller_uid;
	st.st_gid = op_ctx->creds->caller_gid;
	st.st_mode = fsal2unix_mode(attrs_in->mode)
	    & ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	uint32_t create_mask =
		RGW_SETATTR_UID | RGW_SETATTR_GID | RGW_SETATTR_MODE;

	rc = rgw_mkdir(export->rgw_fs, dir->rgw_fh, name, &st, create_mask,
		&rgw_fh, RGW_MKDIR_FLAG_NONE);
	if (rc < 0)
		return rgw2fsal_error(rc);

	rc = construct_handle(export, rgw_fh, &st, &obj);
	if (rc < 0) {
		return rgw2fsal_error(rc);
	}

	*obj_hdl = &obj->handle;

	if (attrs_out != NULL) {
		posix2fsal_attributes(&st, attrs_out);

		/* Make sure ATTR_RDATTR_ERR is cleared on success. */
		attrs_out->mask &= ~ATTR_RDATTR_ERR;
	}


	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Freshen and return attributes
 *
 * This function freshens and returns the attributes of the given
 * file.
 *
 * @param[in]  obj_hdl Object to interrogate
 *
 * @return FSAL status.
 */
static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl,
			struct attrlist *attrs)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
		container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' directory handle */
	struct rgw_handle *handle = container_of(obj_hdl, struct rgw_handle,
						handle);
	/* Stat buffer */
	struct stat st;

	rc = rgw_getattr(export->rgw_fs, handle->rgw_fh, &st,
			RGW_GETATTR_FLAG_NONE);

	if (rc < 0) {
		if (attrs->mask & ATTR_RDATTR_ERR) {
			/* Caller asked for error to be visible. */
			attrs->mask = ATTR_RDATTR_ERR;
		}
		return rgw2fsal_error(rc);
	}

	posix2fsal_attributes(&st, attrs);

	/* Make sure ATTR_RDATTR_ERR is cleared on success. */
	attrs->mask &= ~ATTR_RDATTR_ERR;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Set attributes on a file
 *
 * This function sets attributes on a file.
 *
 * @param[in] obj_hdl File to modify.
 * @param[in] attrs      Attributes to set.
 *
 * @return FSAL status.
 */

static fsal_status_t setattrs(struct fsal_obj_handle *obj_hdl,
			struct attrlist *attrs)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
		container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' directory handle */
	struct rgw_handle *handle = container_of(obj_hdl, struct rgw_handle,
						handle);
	/* Stat buffer */
	struct stat st;
	/* Mask of attributes to set */
	uint32_t mask = 0;

	/* apply umask, if mode attribute is to be changed */
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE))
		attrs->mode &= ~op_ctx->fsal_export->exp_ops.
			fs_umask(op_ctx->fsal_export);

	memset(&st, 0, sizeof(struct stat));

	if (attrs->mask & ~rgw_settable_attributes)
		return fsalstat(ERR_FSAL_INVAL, 0);


	if (FSAL_TEST_MASK(attrs->mask, ATTR_SIZE)) {
		rc = rgw_truncate(export->rgw_fs, handle->rgw_fh,
				attrs->filesize, RGW_TRUNCATE_FLAG_NONE);
		if (rc < 0)
			return rgw2fsal_error(rc);
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE)) {
		mask |= RGW_SETATTR_MODE;
		st.st_mode = fsal2unix_mode(attrs->mode);
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_OWNER)) {
		mask |= RGW_SETATTR_UID;
		st.st_uid = attrs->owner;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_GROUP)) {
		mask |= RGW_SETATTR_UID;
		st.st_gid = attrs->group;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME)) {
		mask |= RGW_SETATTR_ATIME;
		st.st_atim = attrs->atime;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME_SERVER)) {
		mask |= RGW_SETATTR_ATIME;
		struct timespec timestamp;

		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (rc != 0)
			return rgw2fsal_error(rc);
		st.st_atim = timestamp;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME)) {
		mask |= RGW_SETATTR_MTIME;
		st.st_mtim = attrs->mtime;
	}
	if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME_SERVER)) {
		mask |= RGW_SETATTR_MTIME;
		struct timespec timestamp;

		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (rc != 0)
			return rgw2fsal_error(rc);
		st.st_mtim = timestamp;
	}

	if (FSAL_TEST_MASK(attrs->mask, ATTR_CTIME)) {
		mask |= RGW_SETATTR_CTIME;
		st.st_ctim = attrs->ctime;
	}

	rc = rgw_setattr(export->rgw_fs, handle->rgw_fh, &st, mask,
		RGW_SETATTR_FLAG_NONE);
	if (rc < 0)
		return rgw2fsal_error(rc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Rename a file
 *
 * This function renames a file, possibly moving it into another
 * directory.  We assume most checks are done by the caller.
 *
 * @param[in] olddir_hdl Source directory
 * @param[in] old_name   Original name
 * @param[in] newdir_hdl Destination directory
 * @param[in] new_name   New name
 *
 * @return FSAL status.
 */

static fsal_status_t rgw_fsal_rename(struct fsal_obj_handle *obj_hdl,
				struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
		container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' object handle */
	struct rgw_handle *olddir = container_of(olddir_hdl, struct rgw_handle,
						handle);
	/* The private 'full' destination directory handle */
	struct rgw_handle *newdir = container_of(newdir_hdl, struct rgw_handle,
						handle);

	rc = rgw_rename(export->rgw_fs, olddir->rgw_fh, old_name,
			newdir->rgw_fh, new_name, RGW_RENAME_FLAG_NONE);
	if (rc < 0)
		return rgw2fsal_error(rc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Remove a name
 *
 * This function removes a name from the filesystem and possibly
 * deletes the associated file.  Directories must be empty to be
 * removed.
 *
 * @param[in] dir_hdl Parent directory
 * @param[in] name    Name to remove
 *
 * @return FSAL status.
 */

static fsal_status_t rgw_fsal_unlink(struct fsal_obj_handle *dir_hdl,
				struct fsal_obj_handle *obj_hdl,
				const char *name)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
		container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' object handle */
	struct rgw_handle *dir = container_of(dir_hdl, struct rgw_handle,
					handle);

	rc = rgw_unlink(export->rgw_fs, dir->rgw_fh, name,
			RGW_UNLINK_FLAG_NONE);
	if (rc < 0)
		return rgw2fsal_error(rc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Open a file for read or write
 *
 * This function opens a file for reading or writing.  No lock is
 * taken, because we assume we are protected by the Cache inode
 * content lock.
 *
 * @param[in] obj_hdl File to open
 * @param[in] openflags  Mode to open in
 *
 * @return FSAL status.
 */

static fsal_status_t rgw_fsal_open(struct fsal_obj_handle *obj_hdl,
			       fsal_openflags_t openflags)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
		container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' object handle */
	struct rgw_handle *handle = container_of(obj_hdl, struct rgw_handle,
						 handle);
	/* Posix open flags */
	int posix_flags = 0;

	if (openflags & FSAL_O_RDWR)
		posix_flags = O_RDWR;
	else if (openflags & FSAL_O_READ)
		posix_flags = O_RDONLY;
	else if (openflags & FSAL_O_WRITE)
		posix_flags = O_WRONLY;

	/* We shouldn't need to lock anything, the content lock
	   should keep the file descriptor protected. */

	if (handle->openflags != FSAL_O_CLOSED)
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);

	rc = rgw_open(export->rgw_fs, handle->rgw_fh, posix_flags,
		RGW_OPEN_FLAG_NONE);
	if (rc < 0) {
		return rgw2fsal_error(rc);
	}

	handle->openflags = openflags;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}


/**
 * @brief Open a file descriptor for read or write and possibly create
 *
 * This function opens a file for read or write, possibly creating it.
 * If the caller is passing a state, it must hold the state_lock
 * exclusive.
 *
 * state can be NULL which indicates a stateless open (such as via the
 * NFS v3 CREATE operation), in which case the FSAL must assure protection
 * of any resources. If the file is being created, such protection is
 * simple since no one else will have access to the object yet, however,
 * in the case of an exclusive create, the common resources may still need
 * protection.
 *
 * If Name is NULL, obj_hdl is the file itself, otherwise obj_hdl is the
 * parent directory.
 *
 * On an exclusive create, the upper layer may know the object handle
 * already, so it MAY call with name == NULL. In this case, the caller
 * expects just to check the verifier.
 *
 * On a call with an existing object handle for an UNCHECKED create,
 * we can set the size to 0.
 *
 * If attributes are not set on create, the FSAL will set some minimal
 * attributes (for example, mode might be set to 0600).
 *
 * If an open by name succeeds and did not result in Ganesha creating a file,
 * the caller will need to do a subsequent permission check to confirm the
 * open. This is because the permission attributes were not available
 * beforehand.
 *
 * @param[in] obj_hdl               File to open or parent directory
 * @param[in,out] state             state_t to use for this operation
 * @param[in] openflags             Mode for open
 * @param[in] createmode            Mode for create
 * @param[in] name                  Name for file if being created or opened
 * @param[in] attrib_set            Attributes to set on created file
 * @param[in] verifier              Verifier to use for exclusive create
 * @param[in,out] new_obj           Newly created object
 * @param[in,out] caller_perm_check The caller must do a permission check
 *
 * @return FSAL status.
 */

fsal_status_t rgw_fsal_open2(struct fsal_obj_handle *obj_hdl,
			struct state_t *state,
			fsal_openflags_t openflags,
			enum fsal_create_mode createmode,
			const char *name,
			struct attrlist *attrib_set,
			fsal_verifier_t verifier,
			struct fsal_obj_handle **new_obj,
			struct attrlist *attrs_out,
			bool *caller_perm_check)
{
	int posix_flags = 0;
	int rc = 0;
	mode_t unix_mode;
	fsal_status_t status = {0, 0};
	struct stat st;
	bool truncated;
	bool setattrs = attrib_set != NULL;
	bool created = false;
	struct attrlist verifier_attr;
	struct rgw_open_state *open_state = NULL;
	struct rgw_file_handle *rgw_fh;
	struct rgw_handle *obj;

	struct rgw_export *export =
		container_of(op_ctx->fsal_export, struct rgw_export, export);

	struct rgw_handle *handle = container_of(obj_hdl, struct rgw_handle,
						handle);
	if (state) {
		open_state = (struct rgw_open_state *) state;
		LogFullDebug(COMPONENT_FSAL,
			"%s called w/open_state %p", __func__, open_state);
	}

	if (setattrs)
		LogAttrlist(COMPONENT_FSAL, NIV_FULL_DEBUG,
			    "attrs ", attrib_set, false);

	fsal2posix_openflags(openflags, &posix_flags);

	truncated = (posix_flags & O_TRUNC) != 0;

	/* Now fixup attrs for verifier if exclusive create */
	if (createmode >= FSAL_EXCLUSIVE) {
		if (!setattrs) {
			/* We need to use verifier_attr */
			attrib_set = &verifier_attr;
			memset(&verifier_attr, 0, sizeof(verifier_attr));
		}

		set_common_verifier(attrib_set, verifier);
	}

	if (!name) {
		/* This is an open by handle */
		if (state) {
			/* Prepare to take the share reservation, but only if we
			 * are called with a valid state (if state is NULL the
			 * caller is a stateless create such as NFS v3 CREATE).
			 */

			/* This can block over an I/O operation. */
			PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);

			/* Check share reservation conflicts. */
			status = check_share_conflict(&handle->share,
						      openflags, false);

			if (FSAL_IS_ERROR(status)) {
				PTHREAD_RWLOCK_unlock(&obj_hdl->lock);
				return status;
			}

			/* Take the share reservation now by updating the
			 * counters.
			 */
			update_share_counters(&handle->share, FSAL_O_CLOSED,
					      openflags);

			PTHREAD_RWLOCK_unlock(&obj_hdl->lock);
		} else {
			/* RGW doesn't have a file descriptor/open abstraction,
			 * and actually forbids concurrent opens;  This is
			 * where more advanced FSALs would fall back to using
			 * a "global" fd--what we always use;  We still need
			 * to take the lock expected by ULP
			 */
#if 0
			my_fd = &hdl->fd;
#endif
			PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);
		}

		rc = rgw_open(export->rgw_fs, handle->rgw_fh, posix_flags,
			(!state) ? RGW_OPEN_FLAG_V3 : RGW_OPEN_FLAG_NONE);
		if (rc < 0) {
			if (!state) {
				/* Release the lock taken above, and return
				 * since there is nothing to undo.
				 */
				PTHREAD_RWLOCK_unlock(&obj_hdl->lock);
				return rgw2fsal_error(rc);
			} else {
				/* Error - need to release the share */
				goto undo_share;
			}
		}

		if (createmode >= FSAL_EXCLUSIVE || truncated) {
			/* refresh attributes */
			rc = rgw_getattr(export->rgw_fs, rgw_fh, &st,
					RGW_GETATTR_FLAG_NONE);
			if (rc < 0) {
				status = rgw2fsal_error(rc);
			} else {
				LogFullDebug(COMPONENT_FSAL,
					"New size = %"PRIx64, st.st_size);
				/* Now check verifier for exclusive, but not for
				 * FSAL_EXCLUSIVE_9P.
				 */
				if (createmode >= FSAL_EXCLUSIVE &&
					createmode != FSAL_EXCLUSIVE_9P &&
					!obj_hdl->obj_ops.check_verifier(
						obj_hdl, verifier)) {
					/* Verifier didn't match */
					status =
						fsalstat(posix2fsal_error(
							EEXIST),
							EEXIST);
				}
			}
		}

		if (!state) {
			/* If no state, release the lock taken above and return
			 * status.
			 */
			PTHREAD_RWLOCK_unlock(&obj_hdl->lock);
			return status;
		}

		if (!FSAL_IS_ERROR(status)) {
			/* Return success. */
			return status;
		}

		/* close on error */
		(void) rgw_close(export->rgw_fs, handle->rgw_fh,
				RGW_CLOSE_FLAG_NONE);

 undo_share:

		/* Can only get here with state not NULL and an error */

		/* On error we need to release our share reservation
		 * and undo the update of the share counters.
		 * This can block over an I/O operation.
		 */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->lock);

		update_share_counters(&handle->share, openflags, FSAL_O_CLOSED);

		PTHREAD_RWLOCK_unlock(&obj_hdl->lock);

		return status;
	} /* !name */

	/* In this path where we are opening by name, we can't check share
	 * reservation yet since we don't have an object_handle yet. If we
	 * indeed create the object handle (there is no race with another
	 * open by name), then there CAN NOT be a share conflict, otherwise
	 * the share conflict will be resolved when the object handles are
	 * merged.
	 */

	if (createmode == FSAL_NO_CREATE) {
		/* Non creation case, librgw doesn't have open by name so we
		 * have to do a lookup and then handle as an open by handle.
		 */
		struct fsal_obj_handle *temp = NULL;

		/* We don't have open by name... */
		status = obj_hdl->obj_ops.lookup(obj_hdl, name, &temp, NULL);

		if (FSAL_IS_ERROR(status)) {
			LogFullDebug(COMPONENT_FSAL,
				     "lookup returned %s",
				     fsal_err_txt(status));
			return status;
		}

		/* Now call ourselves without name and attributes to open. */
		status = obj_hdl->obj_ops.open2(temp, state, openflags,
						FSAL_NO_CREATE, NULL, NULL,
						verifier, new_obj,
						attrs_out,
						caller_perm_check);

		if (FSAL_IS_ERROR(status)) {
			/* Release the object we found by lookup. */
			temp->obj_ops.release(temp);
			LogFullDebug(COMPONENT_FSAL,
				     "open returned %s",
				     fsal_err_txt(status));
		} else {
			/* No permission check was actually done... */
			*caller_perm_check = true;
		}

		return status;
	}

	/* Now add in O_CREAT and O_EXCL.
	 * Even with FSAL_UNGUARDED we try exclusive create first so
	 * we can safely set attributes.
	 */
	if (createmode != FSAL_NO_CREATE) {
		posix_flags |= O_CREAT;

		if (createmode >= FSAL_GUARDED || setattrs)
			posix_flags |= O_EXCL;
	}

	if (setattrs && FSAL_TEST_MASK(attrib_set->mask, ATTR_MODE)) {
		unix_mode = fsal2unix_mode(attrib_set->mode) &
		    ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);
		/* Don't set the mode if we later set the attributes */
		FSAL_UNSET_MASK(attrib_set->mask, ATTR_MODE);
	} else {
		/* Default to mode 0600 */
		unix_mode = 0600;
	}

	memset(&st, 0, sizeof(struct stat)); /* XXX needed? */

	st.st_uid = op_ctx->creds->caller_uid;
	st.st_gid = op_ctx->creds->caller_gid;
	st.st_mode = unix_mode;

	uint32_t create_mask =
		RGW_SETATTR_UID | RGW_SETATTR_GID | RGW_SETATTR_MODE;

	rc = rgw_create(export->rgw_fs, handle->rgw_fh, name, &st, create_mask,
			&rgw_fh, posix_flags, RGW_CREATE_FLAG_NONE);
	if (rc < 0) {
		LogFullDebug(COMPONENT_FSAL,
			     "Create %s failed with %s",
			     name, strerror(-rc));
	}

	/* XXX won't get here, but maybe someday */
	if (rc == -EEXIST && createmode == FSAL_UNCHECKED) {
		/* We tried to create O_EXCL to set attributes and failed.
		 * Remove O_EXCL and retry, also remember not to set attributes.
		 * We still try O_CREAT again just in case file disappears out
		 * from under us.
		 */
		posix_flags &= ~O_EXCL;
		rc = rgw_create(export->rgw_fs, handle->rgw_fh, name, &st,
				create_mask, &rgw_fh, posix_flags,
				RGW_CREATE_FLAG_NONE);

		if (rc < 0) {
			LogFullDebug(COMPONENT_FSAL,
				     "Non-exclusive Create %s failed with %s",
				     name, strerror(-rc));
		}
	}

	if (rc < 0) {
		return rgw2fsal_error(rc);
	}

	/* Remember if we were responsible for creating the file.
	 * Note that in an UNCHECKED retry we MIGHT have re-created the
	 * file and won't remember that. Oh well, so in that rare case we
	 * leak a partially created file if we have a subsequent error in here.
	 * Since we were able to do the permission check even if we were not
	 * creating the file, let the caller know the permission check has
	 * already been done. Note it IS possible in the case of a race between
	 * an UNCHECKED open and an external unlink, we did create the file.
	 */
	created = (posix_flags & O_EXCL) != 0;
	*caller_perm_check = false;

	construct_handle(export, rgw_fh, &st, &obj);

	/* here FSAL_CEPH operates on its (for RGW non-existent) global
	 * fd */
#if 0
	/* If we didn't have a state above, use the global fd. At this point,
	 * since we just created the global fd, no one else can have a
	 * reference to it, and thus we can mamnipulate unlocked which is
	 * handy since we can then call setattr2 which WILL take the lock
	 * without a double locking deadlock.
	 */
	if (my_fd == NULL)
		my_fd = &hdl->fd;

	my_fd->fd = fd;
#endif
	handle->openflags = openflags;

	*new_obj = &obj->handle;

	if (created && setattrs && attrib_set->mask != 0) {
		/* Set attributes using our newly opened file descriptor as the
		 * share_fd if there are any left to set (mode and truncate
		 * have already been handled).
		 *
		 * Note that we only set the attributes if we were responsible
		 * for creating the file.
		 */
		status = (*new_obj)->obj_ops.setattr2(*new_obj,
						      false,
						      state,
						      attrib_set);

		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			(*new_obj)->obj_ops.release(*new_obj);
			*new_obj = NULL;
			goto fileerr;
		}

		if (attrs_out != NULL) {
			status = (*new_obj)->obj_ops.getattrs(*new_obj,
							      attrs_out);
			if (FSAL_IS_ERROR(status) &&
			    (attrs_out->mask & ATTR_RDATTR_ERR) == 0) {
				/* Get attributes failed and caller expected
				 * to get the attributes. Otherwise continue
				 * with attrs_out indicating ATTR_RDATTR_ERR.
				 */
				goto fileerr;
			}
		}
	} else if (attrs_out != NULL) {
		/* Since we haven't set any attributes other than what was set
		 * on create (if we even created), just use the stat results
		 * we used to create the fsal_obj_handle.
		 */
		posix2fsal_attributes(&st, attrs_out);

		/* Make sure ATTR_RDATTR_ERR is cleared on success. */
		attrs_out->mask &= ~ATTR_RDATTR_ERR;
	}

	if (state != NULL) {
		/* Prepare to take the share reservation, but only if we are
		 * called with a valid state (if state is NULL the caller is
		 * a stateless create such as NFS v3 CREATE).
		 */

		/* This can block over an I/O operation. */
		PTHREAD_RWLOCK_wrlock(&(*new_obj)->lock);

		/* Take the share reservation now by updating the counters. */
		update_share_counters(&handle->share, FSAL_O_CLOSED, openflags);

		PTHREAD_RWLOCK_unlock(&(*new_obj)->lock);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:

	/* Close the file we just opened. */
	(void) rgw_close(export->rgw_fs, handle->rgw_fh,
			RGW_CLOSE_FLAG_NONE);

	if (created) {
		/* Remove the file we just created */
		(void) rgw_unlink(export->rgw_fs, handle->rgw_fh, name,
				RGW_UNLINK_FLAG_NONE);
	}

	return status;
}

/**
 * @brief Return the open status of a file
 *
 * This function returns the open status (the open mode last used to
 * open the file, in our case) for a given file.
 *
 * @param[in] obj_hdl File to interrogate.
 *
 * @return Open mode.
 */

static fsal_openflags_t status(struct fsal_obj_handle *obj_hdl)
{
	/* The private 'full' object handle */
	struct rgw_handle *handle = container_of(obj_hdl, struct rgw_handle,
						 handle);

	return handle->openflags;
}

/**
 * @brief Read data from a file
 *
 * This function reads data from an open file.
 *
 * We take no lock, since we assume we are protected by the
 * Cache inode content lock.
 *
 * @param[in]  obj_hdl  File to read
 * @param[in]  offset      Point at which to begin read
 * @param[in]  buffer_size Maximum number of bytes to read
 * @param[out] buffer      Buffer to store data read
 * @param[out] read_amount Count of bytes read
 * @param[out] end_of_file true if the end of file is reached
 *
 * @return FSAL status.
 */

static fsal_status_t rgw_fsal_read(struct fsal_obj_handle *obj_hdl,
			       uint64_t offset, size_t buffer_size,
			       void *buffer, size_t *read_amount,
			       bool *end_of_file)
{
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' object handle */
	struct rgw_handle *handle = container_of(obj_hdl, struct rgw_handle,
						 handle);
	int rc = rgw_read(export->rgw_fs, handle->rgw_fh, offset,
			buffer_size, read_amount, buffer,
			RGW_READ_FLAG_NONE);

	if (rc < 0)
		return rgw2fsal_error(rc);

	*end_of_file = (read_amount == 0);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Write data to file
 *
 * This function writes data to an open file.
 *
 * We take no lock, since we assume we are protected by the Cache
 * inode content lock.
 *
 * @param[in]  obj_hdl   File to write
 * @param[in]  offset       Position at which to write
 * @param[in]  buffer_size  Number of bytes to write
 * @param[in]  buffer       Data to write
 * @param[out] write_amount Number of bytes written
 *
 * @return FSAL status.
 */

static fsal_status_t rgw_fsal_write(struct fsal_obj_handle *obj_hdl,
				uint64_t offset, size_t buffer_size,
				void *buffer, size_t *write_amount,
				bool *fsal_stable)
{
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' object handle */
	struct rgw_handle *handle = container_of(obj_hdl, struct rgw_handle,
						 handle);
	int rc = rgw_write(export->rgw_fs, handle->rgw_fh, offset,
			buffer_size, write_amount, buffer,
			RGW_WRITE_FLAG_NONE);

	if (rc < 0)
		return rgw2fsal_error(rc);

	*fsal_stable = false;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Commit written data
 *
 * This function commits written data to stable storage.  This FSAL
 * commits data from the entire file, rather than within the given
 * range.
 *
 * @param[in] obj_hdl File to commit
 * @param[in] offset     Start of range to commit
 * @param[in] len        Size of range to commit
 *
 * @return FSAL status.
 */

static fsal_status_t commit(struct fsal_obj_handle *obj_hdl,
			    off_t offset,
			    size_t len)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' object handle */
	struct rgw_handle *handle = container_of(obj_hdl, struct rgw_handle,
						 handle);

	rc = rgw_fsync(export->rgw_fs, handle->rgw_fh, RGW_FSYNC_FLAG_NONE);
	if (rc < 0)
		return rgw2fsal_error(rc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Allocate a state_t structure
 *
 * Note that this is not expected to fail since memory allocation is
 * expected to abort on failure.
 *
 * @param[in] exp_hdl               Export state_t will be associated with
 * @param[in] state_type            Type of state to allocate
 * @param[in] related_state         Related state if appropriate
 *
 * @returns a state structure.
 */

struct state_t *alloc_state(struct fsal_export *exp_hdl,
			enum state_type state_type,
			struct state_t *related_state)
{
	return init_state(gsh_calloc(1, sizeof(struct rgw_open_state)),
			exp_hdl, state_type, related_state);
}


/**
 * @brief Close a file
 *
 * This function closes a file, freeing resources used for read/write
 * access and releasing capabilities.
 *
 * @param[in] obj_hdl File to close
 *
 * @return FSAL status.
 */

static fsal_status_t rgw_fsal_close(struct fsal_obj_handle *obj_hdl)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct rgw_export *export =
	    container_of(op_ctx->fsal_export, struct rgw_export, export);
	/* The private 'full' object handle */
	struct rgw_handle *handle = container_of(obj_hdl, struct rgw_handle,
						 handle);

	rc = rgw_close(export->rgw_fs, handle->rgw_fh, RGW_CLOSE_FLAG_NONE);
	if (rc < 0)
		return rgw2fsal_error(rc);

	handle->openflags = FSAL_O_CLOSED;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Write wire handle
 *
 * This function writes a 'wire' handle to be sent to clients and
 * received from the.
 *
 * @param[in]     obj_hdl  Handle to digest
 * @param[in]     output_type Type of digest requested
 * @param[in,out] fh_desc     Location/size of buffer for
 *                            digest/Length modified to digest length
 *
 * @return FSAL status.
 */

static fsal_status_t handle_digest(const struct fsal_obj_handle *obj_hdl,
				   uint32_t output_type,
				   struct gsh_buffdesc *fh_desc)
{
	/* The private 'full' object handle */
	const struct rgw_handle *handle =
	    container_of(obj_hdl, const struct rgw_handle, handle);

	switch (output_type) {
		/* Digested Handles */
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		if (fh_desc->len < sizeof(struct rgw_fh_hk)) {
			LogMajor(COMPONENT_FSAL,
				 "RGW digest_handle: space too small for handle.  Need %zu, have %zu",
				 sizeof(handle->rgw_fh), fh_desc->len);
			return fsalstat(ERR_FSAL_TOOSMALL, 0);
		} else {
			memcpy(fh_desc->addr, &(handle->rgw_fh->fh_hk),
				sizeof(struct rgw_fh_hk));
			fh_desc->len = sizeof(struct rgw_fh_hk);
		}
		break;

	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Give a hash key for file handle
 *
 * This function locates a unique hash key for a given file.
 *
 * @param[in]  obj_hdl The file whose key is to be found
 * @param[out] fh_desc    Address and length of key
 */

static void handle_to_key(struct fsal_obj_handle *obj_hdl,
			  struct gsh_buffdesc *fh_desc)
{
	/* The private 'full' object handle */
	struct rgw_handle *handle = container_of(obj_hdl, struct rgw_handle,
						handle);

	fh_desc->addr = &(handle->rgw_fh->fh_hk);
	fh_desc->len = sizeof(struct rgw_fh_hk);
}

/**
 * @brief Override functions in ops vector
 *
 * This function overrides implemented functions in the ops vector
 * with versions for this FSAL.
 *
 * @param[in] ops Handle operations vector
 */

void handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->release = release;
	ops->lookup = lookup;
	ops->create = rgw_fsal_create;
	ops->mkdir = rgw_fsal_mkdir;
	ops->readdir = rgw_fsal_readdir;
	ops->getattrs = getattrs;
	ops->setattrs = setattrs;
	ops->rename = rgw_fsal_rename;
	ops->unlink = rgw_fsal_unlink;
	ops->open = rgw_fsal_open;
	ops->status = status;
	ops->read = rgw_fsal_read;
	ops->write = rgw_fsal_write;
	ops->commit = commit;
	ops->close = rgw_fsal_close;
	ops->handle_digest = handle_digest;
	ops->handle_to_key = handle_to_key;
	ops->open2 = rgw_fsal_open2;
	ops->status2 = rgw_fsal_status2;
	ops->reopen2 = rgw_fsal_reopen2;
	ops->read2 = rgw_fsal_read2;
	ops->write2 = rgw_fsal_write2;
	ops->commit2 = rgw_fsal_commit2;
	ops->setattr2 = rgw_fsal_setattr2;
	ops->close2 = rgw_fsal_close2;
}
