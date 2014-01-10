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

/* main.c
 * Module core functions
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include "nlm_list.h"
#include "fsal_handle.h"
#include "fsal_internal.h"
#include "FSAL/fsal_init.h"

/* LUSTRE FSAL module private storage
 */

struct lustre_fsal_module {
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;
	fsal_init_info_t fsal_info;
	/* vfsfs_specific_initinfo_t specific_info;  placeholder */
};

const char myname[] = "LUSTRE";
bool pnfs_enabled;
struct lustre_pnfs_parameter pnfs_param;

/* filesystem info for LUSTRE */
static struct fsal_staticfsinfo_t lustre_info = {
	.maxfilesize = 0xFFFFFFFFFFFFFFFFLL, /* (64bits) */
	.maxlink = _POSIX_LINK_MAX,
	.maxnamelen = 1024,
	.maxpathlen = 1024,
	.no_trunc = true,
	.chown_restricted = true,
	.case_insensitive = false,
	.case_preserving = true,
	.lock_support = true,
	.lock_support_owner = false,
	.lock_support_async_block = false,
	.named_attr = true,
	.unique_handles = true,
	.lease_time = {10, 0},
	.acl_support = FSAL_ACLSUPPORT_ALLOW,
	.homogenous = true,
	.supported_attrs = LUSTRE_SUPPORTED_ATTRIBUTES,
	.pnfs_file = true,
};

static struct config_item ds_params[] = {
	CONF_ITEM_IPV4_ADDR("DS_Addr", "127.0.0.1",
			    lustre_pnfs_ds_parameter, ipaddr),
	CONF_ITEM_PORT("DS_Port", 1024, 0xffff, 3260,
		       lustre_pnfs_ds_parameter, ipport), /* use iscsi port */
	CONF_ITEM_UI32("DS_Id", 1, 0xffffffff, 1,
		       lustre_pnfs_ds_parameter, id),
	CONFIG_EOL
};

static void *dataserver_param_mem(void *parent, void *child)
{
	struct lustre_pnfs_parameter *parent_param;
		= (struct lustre_pnfs_parameter *)parent;
	struct lustre_pnfs_parameter *child_param;
		= (struct lustre_pnfs_parameter *)child;

	if (child == NULL) {
		child_param = gsh_calloc(1,
					 sizeof(struct lustre_pnfs_parameter));
		if (child_param != NULL)
			glist_init(&child_param->ds_list);
		return (void *)child_param;
	} else {
		assert(glist_empty(&child_param->ds_list));
		if (child_param->ipaddr_ascii != NULL)
			gsh_free(child_param->ipaddr_ascii);
		gsh_free(child_param);
	}
}

static void dataserver_attach(void *parent, void *child)
{
	struct lustre_pnfs_parameter *parent_param;
		= (struct lustre_pnfs_parameter *)parent;
	struct lustre_pnfs_parameter *child_param;
		= (struct lustre_pnfs_parameter *)child;

	if (child == NULL) {
		glist_init(&parent_param->ds_list);
	} else {
		glist_add_tail(&parent_param->ds_list,
			       &child_param->ds_list);
}

static struct config_item pnfs_params[] = {
	CONF_ITEM_UI32("Stripe_Size", 0, 1024*1024, 64*1024,
		       lustre_pnfs_parameter, stripe_size),
	CONF_ITEM_UI32("Stripe_Width", 0, 128, 8,
		       lustre_pnfs_parameter, stripe_width),
	CONF_ITEM_BLOCK("DataServer", dataserver_param_mem,
			ds_params, dataserver_attach),
	CONFIG_EOL
};

static void *pnfs_param_mem(void *parent, void *child)
{
	if (child == NULL)
		return &pnfs_param;
	else
		return NULL;
}

static void pnfs_attach(void *parent, void *child)
{
	if (child == NULL)
		pnfs_enabled = false;
	else
		pnfs_enabled = true;
}

static struct config_item lustre_params[] = {
	CONF_ITEM_BOOL("link_support", true,
		       fsal_staticfsinfo_t, link_support),
	CONF_ITEM_BOOL("symlink_support", true,
		       fsal_staticfsinfo_t, symlink_support),
	CONF_ITEM_BOOL("cansettime", true,
		       fsal_staticfsinfo_t, cansettime),
	CONF_ITEM_UI32("maxread", 512, 1024*1024, 1048576,
		       fsal_staticfsinfo_t, maxread),
	CONF_ITEM_UI32("maxwrite", 512, 1024*1024, 1048576,
		       fsal_staticfsinfo_t, maxwrite),
	CONF_ITEM_MODE("umask", 0, 0777, 0,
		       fsal_staticfsinfo_t, umask),
	CONF_ITEM_BOOL("auth_xdev_export", false,
		       fsal_staticfsinfo_t, auth_exportpath_xdev),
	CONF_ITEM_MODE("xattr_access_rights", 0, 0777, 0400,
		       fsal_staticfsinfo_t, xattr_access_rights),
	CONF_ITEM_BLOCK("pnfs", pnfs_param_mem,
			pnfs_params, pnfs_attach)
	CONFIG_EOL
};

struct config_block lustre_param = {
	.name = "LUSTRE",
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.lustre",
	.params = lustre_params
};

/* private helper for export object
 */

struct fsal_staticfsinfo_t *lustre_staticinfo(struct fsal_module *hdl)
{
	return &lustre_info;
}

/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t lustre_init_config(struct fsal_module *fsal_hdl,
				 config_file_t config_struct)
{
	struct lustre_fsal_module *lustre_me =
	    container_of(fsal_hdl, struct lustre_fsal_module, fsal);
	int rc;

	memset(&pnfs_param, 0, sizeof(pnfs_param));
	lustre_me->fs_info = lustre_info; /* get a copy of the defaults */
	rc = load_config_from_parse(config_struct,
				    &lustre_param,
				    &lustre_me->fs_info,
				    true);
	if (rc != 0)
		return fsalstat(ERR_FSAL_INVAL, 0);
	display_fsinfo(&lustre_me->fs_info);

	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%" PRIx64,
		     (uint64_t) LUSTRE_SUPPORTED_ATTRIBUTES);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes default = 0x%"PRIx64,
		     lustre_info.supported_attrs);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 lustre_me->fs_info.supported_attrs);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Internal LUSTRE method linkage to export object
 */

fsal_status_t lustre_create_export(struct fsal_module *fsal_hdl,
				   const char *export_path,
				   const char *fs_options,
				   struct exportlist *exp_entry,
				   struct fsal_module *next_fsal,
				   const struct fsal_up_vector *up_ops,
				   struct fsal_export **export);

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* my module private storage
 */

static struct lustre_fsal_module LUSTRE;

/* linkage to the exports and handle ops initializers
 */


MODULE_INIT void lustre_init(void)
{
	int retval;
	struct fsal_module *myself = &LUSTRE.fsal;

	retval =
	    register_fsal(myself, myname, FSAL_MAJOR_VERSION,
			  FSAL_MINOR_VERSION);
	if (retval != 0) {
		fprintf(stderr, "LUSTRE module failed to register");
		return;
	}
	myself->ops->create_export = lustre_create_export;
	myself->ops->init_config = lustre_init_config;
	init_fsal_parameters(&LUSTRE.fsal_info);
}

MODULE_FINI void lustre_unload(void)
{
	int retval;

	retval = unregister_fsal(&LUSTRE.fsal);
	if (retval != 0) {
		fprintf(stderr, "LUSTRE module failed to unregister");
		return;
	}
}
