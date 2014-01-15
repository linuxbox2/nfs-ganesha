/*
 * Copyright © 2012, CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
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
 * @file FSAL_CEPH/main.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @date Thu Jul  5 14:48:33 2012
 *
 * @brief Impelmentation of FSAL module founctions for Ceph
 *
 * This file implements the module functions for the Ceph FSAL, for
 * initialization, teardown, configuration, and creation of namespaces.
 */

#include <stdlib.h>
#include <assert.h>
#include "fsal.h"
#include "fsal_types.h"
#include "FSAL/fsal_init.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_api.h"
#include "internal.h"
#include "abstract_mem.h"

/**
 * A local copy of the handle for this module, so it can be disposed
 * of.
 */
static struct fsal_module *module;

/**
 * The name of this module.
 */
static const char *module_name = "Ceph";

/**
 * @brief Create a new namespace under this FSAL
 *
 * This function creates a new namespace object for the Ceph FSAL.
 *
 * @todo ACE: We do not handle re-exports of the same cluster in a
 * sane way.  Currently we create multiple handles and cache objects
 * pointing to the same one.  This is not necessarily wrong, but it is
 * inefficient.  It may also not be something we expect to use enough
 * to care about.
 *
 * @param[in]     module_in  The supplied module handle
 * @param[in]     path       The path to export
 * @param[in]     options    Export specific options for the FSAL
 * @param[in,out] list_entry Our entry in the export list
 * @param[in]     next_fsal  Next stacked FSAL
 * @param[out]    pub_namespace  Newly created FSAL namespace object
 *
 * @return FSAL status.
 */

static fsal_status_t create_export(struct fsal_module *module, const char *path,
				   const char *options,
				   struct exportlist *list_entry,
				   struct fsal_module *next_fsal,
				   const struct fsal_up_vector *up_ops,
				   struct fsal_namespace **pub_namespace)
{
	/* The status code to return */
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	/* True if we have called fsal_namespace_init */
	bool initialized = false;
	/* The internal namespace object */
	struct namespace *namespace = NULL;
	/* A fake argument list for Ceph */
	const char *argv[] = { "FSAL_CEPH", path };
	/* Return code from Ceph calls */
	int ceph_status = 0;
	/* Root inode */
	struct Inode *i = NULL;
	/* Root vindoe */
	vinodeno_t root;
	/* Stat for root */
	struct stat st;
	/* Return code */
	int rc = 0;
	/* The 'private' root handle */
	struct handle *handle = NULL;

	if ((path == NULL) || (strlen(path) == 0)) {
		status.major = ERR_FSAL_INVAL;
		LogCrit(COMPONENT_FSAL, "No path to export.");
		goto error;
	}

	if (next_fsal != NULL) {
		status.major = ERR_FSAL_INVAL;
		LogCrit(COMPONENT_FSAL, "Stacked FSALs unsupported.");
		goto error;
	}

	namespace = gsh_calloc(1, sizeof(struct namespace));
	if (namespace == NULL) {
		status.major = ERR_FSAL_NOMEM;
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate namespace object for %s.", path);
		goto error;
	}

	if (fsal_namespace_init(&namespace->namespace, list_entry) != 0) {
		status.major = ERR_FSAL_NOMEM;
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate namespace ops vectors for %s.",
			path);
		goto error;
	}
	namespace_ops_init(namespace->namespace.ops);
	handle_ops_init(namespace->namespace.obj_ops);
#ifdef CEPH_PNFS
	ds_ops_init(namespace->namespace.ds_ops);
#endif				/* CEPH_PNFS */
	namespace->namespace.up_ops = up_ops;

	initialized = true;

	/* allocates ceph_mount_info */
	ceph_status = ceph_create(&namespace->cmount, NULL);
	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to create Ceph handle");
		goto error;
	}

	ceph_status = ceph_conf_read_file(namespace->cmount, NULL);
	if (ceph_status == 0)
		ceph_status = ceph_conf_parse_argv(namespace->cmount, 2, argv);

	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to read Ceph configuration");
		goto error;
	}

	ceph_status = ceph_mount(namespace->cmount, NULL);
	if (ceph_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to mount Ceph cluster.");
		goto error;
	}

	if (fsal_attach_namespace(module, &namespace->namespace.namespaces)
	    != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to attach namespace.");
		goto error;
	}

	namespace->namespace.fsal = module;
	namespace->namespace.fsal = module;

	root.ino.val = CEPH_INO_ROOT;
	root.snapid.val = CEPH_NOSNAP;
	i = ceph_ll_get_inode(namespace->cmount, root);
	if (!i) {
		status.major = ERR_FSAL_SERVERFAULT;
		goto error;
	}

	rc = ceph_ll_getattr(namespace->cmount, i, &st, 0, 0);
	if (rc < 0) {
		status = ceph2fsal_error(rc);
		goto error;
	}

	rc = construct_handle(&st, i, namespace, &handle);
	if (rc < 0) {
		status = ceph2fsal_error(rc);
		goto error;
	}

	namespace->root = handle;
	*pub_namespace = &namespace->namespace;
	return status;

 error:

	if (i) {
		ceph_ll_put(namespace->cmount, i);
		i = NULL;
	}

	if (namespace->cmount != NULL) {
		ceph_shutdown(namespace->cmount);
		namespace->cmount = NULL;
	}

	if (initialized) {
		pthread_mutex_destroy(&namespace->namespace.lock);
		initialized = false;
	}

	if (namespace != NULL) {
		gsh_free(namespace);
		namespace = NULL;
	}

	return status;
}

/**
 * @brief Initialize and register the FSAL
 *
 * This function initializes the FSAL module handle, being called
 * before any configuration or even mounting of a Ceph cluster.  It
 * exists solely to produce a properly constructed FSAL module
 * handle.  Currently, we have no private, per-module data or
 * initialization.
 */

MODULE_INIT void init(void)
{
	/* register_fsal seems to expect zeroed memory. */
	module = gsh_calloc(1, sizeof(struct fsal_module));
	if (module == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate memory for Ceph FSAL module.");
		return;
	}

	if (register_fsal
	    (module, module_name, FSAL_MAJOR_VERSION,
	     FSAL_MINOR_VERSION) != 0) {
		/* The register_fsal function prints its own log
		   message if it fails */
		gsh_free(module);
		LogCrit(COMPONENT_FSAL, "Ceph module failed to register.");
	}

	/* Set up module operations */
	module->ops->create_export = create_export;
}

/**
 * @brief Release FSAL resources
 *
 * This function unregisters the FSAL and frees its module handle.
 * The Ceph FSAL has no other resources to release on the per-FSAL
 * level.
 */

MODULE_FINI void finish(void)
{
	if (unregister_fsal(module) != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to unload FSAL.  Dying with extreme "
			"prejudice.");
		abort();
	}

	gsh_free(module);
	module = NULL;
}
