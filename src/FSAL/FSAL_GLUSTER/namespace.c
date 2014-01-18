/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Red Hat  Inc., 2013
 * Author: Anand Subramanian anands@redhat.com
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * -------------
 */

/**
 * @file  namespace.c
 * @author Shyamsundar R <srangana@redhat.com>
 * @author Anand Subramanian <anands@redhat.com>
 *
 * @brief GLUSTERFS FSAL namespace object
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include "fsal.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "gluster_internal.h"

/**
 * @brief Implements GLUSTER FSAL namespace operation release
 */

static fsal_status_t namespace_release(struct fsal_namespace *namespace)
{
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glusterfs_namespace *glfs_namespace =
	    container_of(namespace, struct glusterfs_namespace, namespace);

	/* check activity on the namespace */
	pthread_mutex_lock(&glfs_namespace->namespace.lock);
	if ((glfs_namespace->namespace.refs > 0)
	    || (!glist_empty(&glfs_namespace->namespace.handles))) {
		pthread_mutex_unlock(&glfs_namespace->namespace.lock);
		status.major = ERR_FSAL_INVAL;
		return status;
	}

	/* detach the namespace */
	fsal_detach_namespace(glfs_namespace->namespace.fsal,
			      &glfs_namespace->namespace.namespaces);
	free_namespace_ops(&glfs_namespace->namespace);
	glfs_namespace->namespace.ops = NULL;
	pthread_mutex_unlock(&glfs_namespace->namespace.lock);

	/* Gluster and memory cleanup */
	glfs_fini(glfs_namespace->gl_fs);
	glfs_namespace->gl_fs = NULL;
	gsh_free(glfs_namespace->export_path);
	glfs_namespace->export_path = NULL;
	pthread_mutex_destroy(&glfs_namespace->namespace.lock);
	gsh_free(glfs_namespace);
	glfs_namespace = NULL;

	return status;
}

/**
 * @brief Implements GLUSTER FSAL namespace operation lookup_path
 */

static fsal_status_t lookup_path(struct fsal_namespace *namespace,
				 const struct req_op_context *opctx,
				 const char *path,
				 struct fsal_obj_handle **pub_handle)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	char *realpath;
	struct stat sb;
	struct glfs_object *glhandle = NULL;
	unsigned char globjhdl[GLAPI_HANDLE_LENGTH];
	struct glusterfs_handle *objhandle = NULL;
	struct glusterfs_namespace *glfs_namespace =
	    container_of(namespace, struct glusterfs_namespace, namespace);

	LogFullDebug(COMPONENT_FSAL, "In args: path = %s", path);

	*pub_handle = NULL;
	realpath = glfs_namespace->export_path;

	glhandle = glfs_h_lookupat(glfs_namespace->gl_fs, NULL, realpath, &sb);
	if (glhandle == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_h_extract_handle(glhandle, globjhdl, GLAPI_HANDLE_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = construct_handle(glfs_namespace, &sb, glhandle, globjhdl,
			      GLAPI_HANDLE_LENGTH, &objhandle);
	if (rc != 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	*pub_handle = &objhandle->handle;

	return status;
 out:
	gluster_cleanup_vars(glhandle);

	return status;
}

/**
 * @brief Implements GLUSTER FSAL namespace operation extract_handle
 */

static fsal_status_t extract_handle(struct fsal_namespace *namespace,
				    fsal_digesttype_t in_type,
				    struct gsh_buffdesc *fh_desc)
{
	size_t fh_size;
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	/* sanity checks */
	if (!fh_desc || !fh_desc->addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	fh_size = GLAPI_HANDLE_LENGTH;
	if (in_type == FSAL_DIGEST_NFSV2) {
		if (fh_desc->len < fh_size) {
			LogMajor(COMPONENT_FSAL,
				 "V2 size too small for handle.  should be %lu, got %lu",
				 fh_size, fh_desc->len);
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
		}
	} else if (in_type != FSAL_DIGEST_SIZEOF && fh_desc->len != fh_size) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be %lu, got %lu",
			 fh_size, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	fh_desc->len = fh_size;	/* pass back the actual size */

#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_extract_handle);
#endif
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Implements GLUSTER FSAL namespace operation create_handle
 */

static fsal_status_t create_handle(struct fsal_namespace *namespace,
				   const struct req_op_context *opctx,
				   struct gsh_buffdesc *fh_desc,
				   struct fsal_obj_handle **pub_handle)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct stat sb;
	struct glfs_object *glhandle = NULL;
	unsigned char globjhdl[GLAPI_HANDLE_LENGTH];
	struct glusterfs_handle *objhandle = NULL;
	struct glusterfs_namespace *glfs_namespace =
	    container_of(namespace, struct glusterfs_namespace, namespace);
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	*pub_handle = NULL;

	if (fh_desc->len != GLAPI_HANDLE_LENGTH) {
		status.major = ERR_FSAL_INVAL;
		goto out;
	}

	memcpy(globjhdl, fh_desc->addr, 16);

	glhandle =
	    glfs_h_create_from_handle(glfs_namespace->gl_fs, globjhdl,
				      GLAPI_HANDLE_LENGTH, &sb);
	if (glhandle == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = construct_handle(glfs_namespace, &sb, glhandle, globjhdl,
			      GLAPI_HANDLE_LENGTH, &objhandle);
	if (rc != 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	*pub_handle = &objhandle->handle;
 out:
	if (status.major != ERR_FSAL_NO_ERROR) {
		gluster_cleanup_vars(glhandle);
	}
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_create_handle);
#endif
	return status;
}

/**
 * @brief Implements GLUSTER FSAL namespace operation get_fs_dynamic_info
 */

static fsal_status_t get_dynamic_info(struct fsal_namespace *namespace,
				      const struct req_op_context *opctx,
				      fsal_dynamicfsinfo_t * infop)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct statvfs vfssb;
	struct glusterfs_namespace *glfs_namespace =
	    container_of(namespace, struct glusterfs_namespace, namespace);

	rc = glfs_statvfs(glfs_namespace->gl_fs, glfs_namespace->export_path, &vfssb);
	if (rc != 0) {
		return gluster2fsal_error(rc);
	}

	memset(infop, 0, sizeof(fsal_dynamicfsinfo_t));
	infop->total_bytes = vfssb.f_frsize * vfssb.f_blocks;
	infop->free_bytes = vfssb.f_frsize * vfssb.f_bfree;
	infop->avail_bytes = vfssb.f_frsize * vfssb.f_bavail;
	infop->total_files = vfssb.f_files;
	infop->free_files = vfssb.f_ffree;
	infop->avail_files = vfssb.f_favail;
	infop->time_delta.tv_sec = 1;
	infop->time_delta.tv_nsec = 0;

	return status;
}

/* TODO: We have gone POSIX way for the APIs below, can consider the CEPH way
 * in case all are constants across all volumes etc. */

/**
 * @brief Implements GLUSTER FSAL namespace operation fs_supports
 */

static bool fs_supports(struct fsal_namespace *namespace,
			fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(namespace->fsal);
	return fsal_supports(info, option);
}

/**
 * @brief Implements GLUSTER FSAL namespace operation fs_maxfilesize
 */

static uint64_t fs_maxfilesize(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(namespace->fsal);
	return fsal_maxfilesize(info);
}

/**
 * @brief Implements GLUSTER FSAL namespace operation fs_maxread
 */

static uint32_t fs_maxread(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(namespace->fsal);
	return fsal_maxread(info);
}

/**
 * @brief Implements GLUSTER FSAL namespace operation fs_maxwrite
 */

static uint32_t fs_maxwrite(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(namespace->fsal);
	return fsal_maxwrite(info);
}

/**
 * @brief Implements GLUSTER FSAL namespace operation fs_maxlink
 */

static uint32_t fs_maxlink(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(namespace->fsal);
	return fsal_maxlink(info);
}

/**
 * @brief Implements GLUSTER FSAL namespace operation fs_maxnamelen
 */

static uint32_t fs_maxnamelen(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(namespace->fsal);
	return fsal_maxnamelen(info);
}

/**
 * @brief Implements GLUSTER FSAL namespace operation fs_maxpathlen
 */

static uint32_t fs_maxpathlen(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(namespace->fsal);
	return fsal_maxpathlen(info);
}

/**
 * @brief Implements GLUSTER FSAL namespace operation fs_lease_time
 */

static struct timespec fs_lease_time(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(namespace->fsal);
	return fsal_lease_time(info);
}

/**
 * @brief Implements GLUSTER FSAL namespace operation fs_acl_support
 */

static fsal_aclsupp_t fs_acl_support(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(namespace->fsal);
	return fsal_acl_support(info);
}

/**
 * @brief Implements GLUSTER FSAL namespace operation fs_supported_attrs
 */

static attrmask_t fs_supported_attrs(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(namespace->fsal);
	return fsal_supported_attrs(info);
}

/**
 * @brief Implements GLUSTER FSAL namespace operation fs_umask
 */

static uint32_t fs_umask(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(namespace->fsal);
	return fsal_umask(info);
}

/**
 * @brief Implements GLUSTER FSAL namespace operation fs_xattr_access_rights
 */

static uint32_t fs_xattr_access_rights(struct fsal_namespace *namespace)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(namespace->fsal);
	return fsal_xattr_access_rights(info);
}

/**
 * @brief Implements GLUSTER FSAL namespace operation check_quota
 */
/*
static fsal_status_t check_quota(struct fsal_namespace *namespace,
				 const char * filepath,
				 int quota_type,
				 struct req_op_context *req_ctx)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0) ;
}
*/
/**
 * @brief Implements GLUSTER FSAL namespace operation get_quota
 */
/*
static fsal_status_t get_quota(struct fsal_namespace *namespace,
			       const char * filepath,
			       int quota_type,
			       struct req_op_context *req_ctx,
			       fsal_quota_t *pquota)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0) ;
}
*/
/**
 * @brief Implements GLUSTER FSAL namespace operation set_quota
 */
/*
static fsal_status_t set_quota(struct fsal_namespace *namespace,
			       const char *filepath,
			       int quota_type,
			       struct req_op_context *req_ctx,
			       fsal_quota_t * pquota,
			       fsal_quota_t * presquota)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0) ;
}
*/

/**
 * @brief Registers GLUSTER FSAL namespace operation vector
 *
 * This function overrides operations that we've implemented, leaving
 * the rest for the default.
 *
 * @param[in,out] ops Operations vector
 */

void namespace_ops_init(struct namespace_ops *ops)
{
	ops->release = namespace_release;
	ops->lookup_path = lookup_path;
	ops->extract_handle = extract_handle;
	ops->create_handle = create_handle;
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
}

void handle_ops_init(struct fsal_obj_ops *ops);

/**
 * @brief Implements GLUSTER FSAL moduleoperation create_export
 */

fsal_status_t glusterfs_create_export(struct fsal_module *fsal_hdl,
				      const char *export_path,
				      const char *fs_options,
				      struct exportlist *exp_entry,
				      struct fsal_module *next_fsal,
				      const struct fsal_up_vector *up_ops,
				      struct fsal_namespace **namespace)
{
	int rc;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glusterfs_namespace *glfsnamespace = NULL;
	glfs_t *fs = NULL;
	char *glvolname = NULL, *glhostname = NULL, *glvolpath = NULL;
	int oplen = 0, namespace_inited = 0;

	LogDebug(COMPONENT_FSAL, "In args: export path = %s, fs options = %s",
		 export_path, fs_options);

	if ((NULL == export_path) || (strlen(export_path) == 0)) {
		status.major = ERR_FSAL_INVAL;
		LogCrit(COMPONENT_FSAL, "No path to export.");
		goto out;
	}

	if ((NULL == fs_options) || (strlen(fs_options) == 0)) {
		status.major = ERR_FSAL_INVAL;
		LogCrit(COMPONENT_FSAL,
			"Missing FS specific information. Export: %s",
			export_path);
		goto out;
	}

	if (next_fsal != NULL) {
		status.major = ERR_FSAL_INVAL;
		LogCrit(COMPONENT_FSAL, "Stacked FSALs unsupported. Export: %s",
			export_path);
		goto out;
	}

	/* Process FSSpecific Gluster volume name */
	if (!(fs_specific_has(fs_options, GLUSTER_VOLNAME_KEY, NULL, &oplen))) {
		status.major = ERR_FSAL_INVAL;
		LogCrit(COMPONENT_FSAL,
			"FS specific missing gluster volume name. Export: %s",
			export_path);
		goto out;
	}
	glvolname = gsh_calloc(oplen, sizeof(char));
	if (glvolname == NULL) {
		status.major = ERR_FSAL_NOMEM;
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate volume name bytes %d.  Export: %s",
			oplen, export_path);
		goto out;
	}
	fs_specific_has(fs_options, GLUSTER_VOLNAME_KEY, glvolname, &oplen);

	/* Process FSSpecific Gluster host name */
	if (!(fs_specific_has(fs_options, GLUSTER_HOSTNAME_KEY, NULL, &oplen))) {
		status.major = ERR_FSAL_INVAL;
		LogCrit(COMPONENT_FSAL,
			"FS specific missing gluster hostname or IP address. Export: %s",
			export_path);
		goto out;
	}
	glhostname = gsh_calloc(oplen, sizeof(char));
	if (glhostname == NULL) {
		status.major = ERR_FSAL_NOMEM;
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate host name bytes %d.  Export: %s",
			oplen, export_path);
		goto out;
	}
	fs_specific_has(fs_options, GLUSTER_HOSTNAME_KEY, glhostname, &oplen);

	/* Process FSSpecific Gluster volume path (optional) */
	if (!(fs_specific_has(fs_options, GLUSTER_VOLPATH_KEY, NULL, &oplen))) {
		LogEvent(COMPONENT_FSAL, "Volume %s exported at : '/'",
			 glvolname);

		glvolpath = gsh_calloc(2, sizeof(char));
		if (glvolpath == NULL) {
			status.major = ERR_FSAL_NOMEM;
			LogCrit(COMPONENT_FSAL,
				"Unable to allocate volume path bytes 2.  Export: %s",
				export_path);
			goto out;
		}
		strcpy(glvolpath, "/");
	} else {
		glvolpath = gsh_calloc(oplen, sizeof(char));
		if (glvolpath == NULL) {
			status.major = ERR_FSAL_NOMEM;
			LogCrit(COMPONENT_FSAL,
				"Unable to allocate host name bytes %d.  Export: %s",
				oplen, export_path);
			goto out;
		}
		fs_specific_has(fs_options, GLUSTER_VOLPATH_KEY, glvolpath,
				&oplen);
		LogEvent(COMPONENT_FSAL, "Volume %s exported at : '%s'",
			 glvolname, glvolpath);
	}

	glfsnamespace = gsh_calloc(1, sizeof(struct glusterfs_namespace));
	if (glfsnamespace == NULL) {
		status.major = ERR_FSAL_NOMEM;
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate namespace object.  Export: %s",
			export_path);
		goto out;
	}

	if (fsal_namespace_init(&glfsnamespace->namespace, exp_entry) != 0) {
		status.major = ERR_FSAL_NOMEM;
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate namespace ops vectors.  Export: %s",
			export_path);
		goto out;
	}

	namespace_ops_init(glfsnamespace->namespace.ops);
	handle_ops_init(glfsnamespace->namespace.obj_ops);
	glfsnamespace->namespace.up_ops = up_ops;
	namespace_inited = 1;

	fs = glfs_new(glvolname);
	if (!fs) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to create new glfs. Export: %s",
			export_path);
		goto out;
	}

	rc = glfs_set_volfile_server(fs, "tcp", glhostname, 24007);
	if (rc != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to set volume file. Export: %s",
			export_path);
		goto out;
	}

	rc = glfs_set_logging(fs, "/tmp/gfapi.log", 7);
	if (rc != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to set logging. Export: %s",
			export_path);
		goto out;
	}

	rc = glfs_init(fs);
	if (rc != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to initialize volume. Export: %s", export_path);
		goto out;
	}

	if ((rc =
	     fsal_attach_namespace(fsal_hdl, &glfsnamespace->namespace.namespaces)) != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to attach namespace. Export: %s",
			export_path);
		goto out;
	}

	glfsnamespace->export_path = glvolpath;
	glfsnamespace->gl_fs = fs;
	glfsnamespace->saveduid = geteuid();
	glfsnamespace->savedgid = getegid();
	glfsnamespace->namespace.fsal = fsal_hdl;

	*namespace = &glfsnamespace->namespace;

 out:
	if (glvolname)
		gsh_free(glvolname);
	if (glhostname)
		gsh_free(glhostname);

	if (status.major != ERR_FSAL_NO_ERROR) {
		if (glvolpath)
			gsh_free(glvolpath);

		if (namespace_inited)
			pthread_mutex_destroy(&glfsnamespace->namespace.lock);

		if (fs)
			glfs_fini(fs);

		if (glfsnamespace)
			gsh_free(glfsnamespace);
	}

	return status;
}
