/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Max Matveev, 2012
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
 */

/* Namespace-related methods */

#include "config.h"

#include "fsal.h"
#include <pthread.h>
#include <sys/types.h>
#include "nlm_list.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "pxy_fsal_methods.h"
#include <nfs_exports.h>

static fsal_status_t pxy_release(struct fsal_namespace *namespace)
{
	struct pxy_namespace *pxy_namespace =
	    container_of(namespace, struct pxy_namespace, namespace);

	pthread_mutex_lock(&namespace->lock);
	if (namespace->refs > 0 || !glist_empty(&namespace->handles)) {
		pthread_mutex_unlock(&namespace->lock);
		return fsalstat(ERR_FSAL_INVAL, EBUSY);
	}
	fsal_detach_namespace(namespace->fsal, &namespace->namespaces);
	free_namespace_ops(namespace);
	pthread_mutex_unlock(&namespace->lock);

	pthread_mutex_destroy(&namespace->lock);
	gsh_free(pxy_namespace);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static bool pxy_get_supports(struct fsal_namespace *namespace,
			     fsal_fsinfo_options_t option)
{
	struct pxy_fsal_module *pm =
	    container_of(namespace->fsal, struct pxy_fsal_module, module);
	return fsal_supports(&pm->fsinfo, option);
}

static uint64_t pxy_get_maxfilesize(struct fsal_namespace *namespace)
{
	struct pxy_fsal_module *pm =
	    container_of(namespace->fsal, struct pxy_fsal_module, module);
	return fsal_maxfilesize(&pm->fsinfo);
}

static uint32_t pxy_get_maxread(struct fsal_namespace *namespace)
{
	struct pxy_fsal_module *pm =
	    container_of(namespace->fsal, struct pxy_fsal_module, module);
	return fsal_maxread(&pm->fsinfo);
}

static uint32_t pxy_get_maxwrite(struct fsal_namespace *namespace)
{
	struct pxy_fsal_module *pm =
	    container_of(namespace->fsal, struct pxy_fsal_module, module);
	return fsal_maxwrite(&pm->fsinfo);
}

static uint32_t pxy_get_maxlink(struct fsal_namespace *namespace)
{
	struct pxy_fsal_module *pm =
	    container_of(namespace->fsal, struct pxy_fsal_module, module);
	return fsal_maxlink(&pm->fsinfo);
}

static uint32_t pxy_get_maxnamelen(struct fsal_namespace *namespace)
{
	struct pxy_fsal_module *pm =
	    container_of(namespace->fsal, struct pxy_fsal_module, module);
	return fsal_maxnamelen(&pm->fsinfo);
}

static uint32_t pxy_get_maxpathlen(struct fsal_namespace *namespace)
{
	struct pxy_fsal_module *pm =
	    container_of(namespace->fsal, struct pxy_fsal_module, module);
	return fsal_maxpathlen(&pm->fsinfo);
}

static struct timespec pxy_get_lease_time(struct fsal_namespace *namespace)
{
	struct pxy_fsal_module *pm =
	    container_of(namespace->fsal, struct pxy_fsal_module, module);
	return fsal_lease_time(&pm->fsinfo);
}

static fsal_aclsupp_t pxy_get_acl_support(struct fsal_namespace *namespace)
{
	struct pxy_fsal_module *pm =
	    container_of(namespace->fsal, struct pxy_fsal_module, module);
	return fsal_acl_support(&pm->fsinfo);
}

static attrmask_t pxy_get_supported_attrs(struct fsal_namespace *namespace)
{
	struct pxy_fsal_module *pm =
	    container_of(namespace->fsal, struct pxy_fsal_module, module);
	return fsal_supported_attrs(&pm->fsinfo);
}

static uint32_t pxy_get_umask(struct fsal_namespace *namespace)
{
	struct pxy_fsal_module *pm =
	    container_of(namespace->fsal, struct pxy_fsal_module, module);
	return fsal_umask(&pm->fsinfo);
}

static uint32_t pxy_get_xattr_access_rights(struct fsal_namespace *namespace)
{
	struct pxy_fsal_module *pm =
	    container_of(namespace->fsal, struct pxy_fsal_module, module);
	return fsal_xattr_access_rights(&pm->fsinfo);
}

void pxy_namespace_ops_init(struct namespace_ops *ops)
{
	ops->release = pxy_release;
	ops->lookup_path = pxy_lookup_path;
	ops->extract_handle = pxy_extract_handle;
	ops->create_handle = pxy_create_handle;
	ops->get_fs_dynamic_info = pxy_get_dynamic_info;
	ops->fs_supports = pxy_get_supports;
	ops->fs_maxfilesize = pxy_get_maxfilesize;
	ops->fs_maxread = pxy_get_maxread;
	ops->fs_maxwrite = pxy_get_maxwrite;
	ops->fs_maxlink = pxy_get_maxlink;
	ops->fs_maxnamelen = pxy_get_maxnamelen;
	ops->fs_maxpathlen = pxy_get_maxpathlen;
	ops->fs_lease_time = pxy_get_lease_time;
	ops->fs_acl_support = pxy_get_acl_support;
	ops->fs_supported_attrs = pxy_get_supported_attrs;
	ops->fs_umask = pxy_get_umask;
	ops->fs_xattr_access_rights = pxy_get_xattr_access_rights;
};

/* Here and not static because proxy.c needs this function
 * but we also need access to pxy_namespace_ops - I'd rather
 * keep the later static then the former */
fsal_status_t pxy_create_export(struct fsal_module *fsal_hdl,
				const char *export_path, const char *fs_options,
				struct exportlist *exp_entry,
				struct fsal_module *next_fsal,
				const struct fsal_up_vector *up_ops,
				struct fsal_namespace **namespace)
{
	struct pxy_namespace *pxy_namespace =
	    gsh_calloc(1, sizeof(*pxy_namespace));
	struct pxy_fsal_module *pxy =
	    container_of(fsal_hdl, struct pxy_fsal_module, module);

	if (!pxy_namespace)
		return fsalstat(ERR_FSAL_NOMEM, ENOMEM);
	if (fsal_namespace_init(&pxy_namespace->namespace, exp_entry) != 0) {
		gsh_free(pxy_namespace);
		return fsalstat(ERR_FSAL_NOMEM, ENOMEM);
	}
	pxy_namespace_ops_init(pxy_namespace->namespace.ops);
	pxy_handle_ops_init(pxy_namespace->namespace.obj_ops);
	pxy_namespace->namespace.up_ops = up_ops;
	pxy_namespace->info = &pxy->special;
	pxy_namespace->namespace.fsal = fsal_hdl;
	*namespace = &pxy_namespace->namespace;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
