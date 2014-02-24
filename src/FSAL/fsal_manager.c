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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/**
 * @addtogroup FSAL
 * @{
 */

/**
 * @file fsal_manager.c
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief FSAL module manager
 */

#include "config.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <dlfcn.h>
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "config_parsing.h"
#include "pnfs_utils.h"
#include "fsal_private.h"

/**
 * @brief Tree of loaded FSAL modules, by name
 *
 * Tree, for fast name lookups. The fsal_lock must be held.
 */

struct avltree fsal_by_name;

/**
 * @brief Tree of loaded FSAL modules, by ID
 *
 * Tree, for fast ID lookups. The fsal_lock must be held.
 */

struct avltree fsal_by_num;

/**
 * @brief FSAL lock
 *
 * Must be held for tree traversals and similar.
 */

pthread_mutex_t fsal_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief The next ID
 *
 * It must only be read or incremented with the lock held.
 *
 * @todo This must be destroyed and replaced with a statically
 * assigned FSAL ID, to be done by Lieb.
 */

static uint8_t next_fsal_id;

/**
 * @{
 *
 * Variables for passing status/errors between shared object
 * and this module. They must be accessed under lock.
 */

static char *dl_error;
static int so_error;
static struct fsal_module *new_fsal;

/**
 * @}
 */

/**
 * @brief FSAL load state
 */

static enum load_state {
	init,		/*< In server start state. .init sections can run */
	idle,		/*< Switch from init->idle early in main() */
	loading,	/*< In dlopen(). set by load_fsal() just prior */
	registered,	/*< signal by registration that all is well */
	error		/*< signal by registration that all is not well */
} load_state = init;


/**
 * @brief Start the PSEUDOFS FSAL
 *
 * The pseudofs fsal is static (always present) so it needs its own
 * startup.  This is a stripped down version of load_fsal() that is
 * done very early in server startup.
 */

static void load_fsal_pseudo(void)
{
	char *dl_path;
	struct fsal_module *fsal;

	dl_path = gsh_strdup("Builtin-PseudoFS");
	if (dl_path == NULL)
		LogFatal(COMPONENT_INIT, "Couldn't Register FSAL_PSEUDO");

	pthread_mutex_lock(&fsal_lock);

	if (load_state != idle)
		LogFatal(COMPONENT_INIT, "Couldn't Register FSAL_PSEUDO");

	if (dl_error) {
		gsh_free(dl_error);
		dl_error = NULL;
	}

	load_state = loading;

	pthread_mutex_unlock(&fsal_lock);

	/* now it is the module's turn to register itself */
	pseudo_fsal_init();

	pthread_mutex_lock(&fsal_lock);

	if (load_state != registered)
		LogFatal(COMPONENT_INIT, "Couldn't Register FSAL_PSEUDO");

	/* we now finish things up, doing things the module can't see */

	fsal = new_fsal;   /* recover handle from .ctor and poison again */
	new_fsal = NULL;
	fsal->path = dl_path;
	fsal->dl_handle = NULL;
	so_error = 0;
	load_state = idle;
	pthread_mutex_unlock(&fsal_lock);
}

/**
 * @brief Comparison for FSAL names
 *
 * @param[in] node1 A node
 * @param[in] nodea Another node
 *
 * @retval -1 if node1 is less than nodea
 * @retval 0 if node1 and nodea are equal
 * @retval 1 if node1 is greater than nodea
 */

static int name_comparator(const struct avltree_node *node1,
			   const struct avltree_node *nodea)
{
	struct fsal_module *fsal1 =
	    avltree_container_of(node1, struct fsal_module,
				 by_name);
	struct fsal_module *fsala =
	    avltree_container_of(nodea, struct fsal_module,
				 by_name);

	return strcasecmp(fsal1->name, fsala->name);
}

/**
 * @brief Comparison for FSAL IDs
 *
 * @param[in] node1 A node
 * @param[in] nodea Another node
 *
 * @retval -1 if node1 is less than nodea
 * @retval 0 if node1 and nodea are equal
 * @retval 1 if node1 is greater than nodea
 */

static int num_comparator(const struct avltree_node *node1,
			  const struct avltree_node *nodea)
{
	struct fsal_module *fsal1 =
	    avltree_container_of(node1, struct fsal_module,
				 by_num);
	struct fsal_module *fsala =
	    avltree_container_of(nodea, struct fsal_module,
				 by_num);

	if (fsal1->id < fsala->id)
		return -1;
	else if (fsal1->id > fsala->id)
		return 1;
	else
		return 0;
}

/**
 * @brief Start_fsals
 *
 * Called early server initialization.  Set load_state to idle
 * at this point as a check on dynamic loading not starting too early.
 */

void start_fsals(void)
{
	avltree_init(&fsal_by_name, name_comparator, 0);
	avltree_init(&fsal_by_num, num_comparator, 0);

	/* .init was a long time ago... */
	load_state = idle;

	/* Load FSAL_PSEUDO */
	load_fsal_pseudo();
}

/**
 * Enforced filename for FSAL library objects.
 */

static const char *pathfmt = "%s/libfsal%s.so";

/**
 * @brief Load the fsal's shared object.
 *
 * The dlopen() will trigger a .init constructor which will do the
 * actual registration.  after a successful load, the returned handle
 * needs to be "put" back after any other initialization is done.
 *
 * @param[in]  name       Name of the FSAL to load
 * @param[out] fsal_hdl_p Newly allocated FSAL handle
 *
 * @retval 0 Success, when finished, put_fsal_handle() to free
 * @retval EBUSY the loader is busy (should not happen)
 * @retval EEXIST the module is already loaded
 * @retval ENOLCK register_fsal without load_fsal holding the lock.
 * @retval EINVAL wrong loading state for registration
 * @retval ENOMEM out of memory
 * @retval ENOENT could not find "module_init" function
 * @retval EFAULT module_init has a bad address
 * @retval other general dlopen errors are possible, all of them bad
 */

int load_fsal(const char *name,
	      struct fsal_module **fsal_hdl_p)
{
	void *dl;
	int retval = EBUSY;	/* already loaded */
	char *dl_path;
	struct fsal_module *fsal;
	char *bp;
	char *path = alloca(strlen(nfs_param.core_param.ganesha_modules_loc)
			    + strlen(name)
			    + strlen(pathfmt));

	sprintf(path, pathfmt,
		nfs_param.core_param.ganesha_modules_loc,
		name);
	bp = rindex(path, '/');
	bp++; /* now it is the basename, lcase it */
	while (*bp != '\0') {
		if (isupper(*bp))
			*bp = tolower(*bp);
		bp++;
	}
	dl_path = gsh_strdup(path);
	if (dl_path == NULL)
		return ENOMEM;
	pthread_mutex_lock(&fsal_lock);
	if (load_state != idle)
		goto errout;
	if (dl_error) {
		gsh_free(dl_error);
		dl_error = NULL;
	}
#ifdef LINUX
	/* recent linux/glibc can probe to see if it already there */
	LogDebug(COMPONENT_INIT, "Probing to see if %s is already loaded",
		 path);
	dl = dlopen(path, RTLD_NOLOAD);
	if (dl != NULL) {
		retval = EEXIST;
		LogDebug(COMPONENT_INIT, "Already exists ...");
		goto errout;
	}
#endif

	load_state = loading;
	pthread_mutex_unlock(&fsal_lock);

	LogDebug(COMPONENT_INIT, "Loading FSAL %s with %s", name, path);
#ifdef LINUX
	dl = dlopen(path, RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
#elif FREEBSD
	dl = dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif

	pthread_mutex_lock(&fsal_lock);
	if (dl == NULL) {
#ifdef ELIBACC
		retval = ELIBACC;	/* hand craft a meaningful error */
#else
		retval = EPERM;	/* ELIBACC does not exist on MacOS */
#endif
		dl_error = gsh_strdup(dlerror());
		LogCrit(COMPONENT_INIT, "Could not dlopen module:%s Error:%s",
			path, dl_error);
		goto errout;
	}
	dlerror();	/* clear it */

/* now it is the module's turn to register itself */

	if (load_state == loading) {	/* constructor didn't fire */
		void (*module_init) (void);
		char *sym_error;

		module_init = dlsym(dl, "fsal_init");
		sym_error = (char *)dlerror();
		if (sym_error != NULL) {
			dl_error = gsh_strdup(sym_error);
			so_error = ENOENT;
			LogCrit(COMPONENT_INIT,
				"Could not execute symbol fsal_init"
				" from module:%s Error:%s", path, dl_error);
			goto errout;
		}
		if ((void *)module_init == NULL) {
			so_error = EFAULT;
			LogCrit(COMPONENT_INIT,
				"Could not execute symbol fsal_init"
				" from module:%s Error:%s", path, dl_error);
			goto errout;
		}
		pthread_mutex_unlock(&fsal_lock);

		(*module_init) ();	/* try registering by hand this time */

		pthread_mutex_lock(&fsal_lock);
	}
	if (load_state == error) {	/* we are in registration hell */
		dlclose(dl);
		retval = so_error;	/* this is the registration error */
		LogCrit(COMPONENT_INIT,
			"Could not execute symbol fsal_init"
			" from module:%s Error:%s", path, dl_error);
		goto errout;
	}
	if (load_state != registered) {
		retval = EPERM;
		LogCrit(COMPONENT_INIT,
			"Could not execute symbol fsal_init"
			" from module:%s Error:%s", path, dl_error);
		goto errout;
	}

/* we now finish things up, doing things the module can't see */

	fsal = new_fsal;   /* recover handle from .ctor and poison again */
	new_fsal = NULL;
	fsal->refs++; /* take initial ref so we can pass it back... */
	fsal->path = dl_path;
	fsal->dl_handle = dl;
	so_error = 0;
	*fsal_hdl_p = fsal;
	load_state = idle;
	pthread_mutex_unlock(&fsal_lock);
	return 0;

 errout:
	load_state = idle;
	pthread_mutex_unlock(&fsal_lock);
	LogMajor(COMPONENT_INIT, "Failed to load module (%s) because: %s",
		 path,
		 strerror(retval));
	gsh_free(dl_path);
	return retval;
}

/**
 * @brief Look up an FSAL by name
 *
 * Acquire a handle to the named FSAL and take a reference to it. This
 * must be done before using any methods.  Once done, release it with
 * @c put_fsal.
 *
 * @param[in] name Name to look up
 *
 * @return Module pointer or NULL if not found.
 */

struct fsal_module *lookup_fsal(const char *name)
{
	struct fsal_module *fsal;
	struct fsal_module prototype = {
		.name = name
	};
	struct avltree_node *found;

	pthread_mutex_lock(&fsal_lock);
	found = avltree_lookup(&prototype.by_name, &fsal_by_name);

	if (unlikely(!found)) {
		pthread_mutex_unlock(&fsal_lock);
		return NULL;
	}
	fsal = avltree_container_of(found, struct fsal_module,
				    by_name);

	/**
	 * @warning This is locking behavior differs from the
	 * original. The original locked before doing the comparison,
	 * this locks after finding the node. This should be fine,
	 * since the name and ID are only modified by unregister_fsal.
	 * unregister_fsal should only be called when an FSAL is
	 * removed from the tree which should require the tree lock
	 * which we have. If this is not the case, then we will have
	 * to hack at the avltree code or do something deliciously
	 * perverse and delightfully eeeeeeevil with the prototype and
	 * comparator.
	 */
	pthread_mutex_lock(&fsal->lock);
	fsal->refs++;
	pthread_mutex_unlock(&fsal->lock);
	pthread_mutex_unlock(&fsal_lock);
	return fsal;
}

/**
 * @brief Look up an FSAL by ID
 *
 * Acquire a handle to the denumerated FSAL and take a reference to
 * it. This must be done before using any methods.  Once done, release
 * it with @c put_fsal.
 *
 * @param[in] num ID to look up
 *
 * @return Module pointer or NULL if not found.
 */

struct fsal_module *lookup_fsal_id(const uint8_t num)
{
	struct fsal_module *fsal;
	struct fsal_module prototype = {
		.id = num
	};
	struct avltree_node *found;

	pthread_mutex_lock(&fsal_lock);
	found = avltree_lookup(&prototype.by_num, &fsal_by_num);

	if (unlikely(!found)) {
		pthread_mutex_unlock(&fsal_lock);
		return NULL;
	}
	fsal = avltree_container_of(found, struct fsal_module,
				    by_name);

	/**
	 * @warning See warning for lookup_fsal
	 */
	pthread_mutex_lock(&fsal->lock);
	fsal->refs++;
	pthread_mutex_unlock(&fsal->lock);
	pthread_mutex_unlock(&fsal_lock);
	return fsal;
}

/* functions only called by modules at ctor/dtor time
 */

/**
 * @brief Register the fsal in the system
 *
 * This can be called from three places:
 *
 *  + the server program's .init section if the fsal was statically linked
 *  + the shared object's .init section when load_fsal() dynamically loads it.
 *  + from the shared object's 'fsal_init' function if dlopen does not support
 *    .init/.fini sections.
 *
 * Any other case is an error.
 * Change load_state only for dynamically loaded modules.
 *
 * @note We use an ADAPTIVE_NP mutex because the initial spinlock is low
 * impact for protecting the list add/del atomicity.  Does FBSD have this?
 *
 * @param[in] fsal_hdl      FSAL module handle
 * @param[in] name          FSAL name
 * @param[in] major_version Major version
 * @param[in] minor_version Minor version
 *
 * @return 0 on success, otherwise POSIX errors.
 */

/** @todo implement api versioning and pass the major,minor here
 */

int register_fsal(struct fsal_module *fsal_hdl, const char *name,
		  uint32_t major_version, uint32_t minor_version)
{
	pthread_mutexattr_t attrs;

	if ((major_version != FSAL_MAJOR_VERSION)
	    || (minor_version > FSAL_MINOR_VERSION)) {
		so_error = EINVAL;
		LogCrit(COMPONENT_INIT,
			"FSAL \"%s\" failed to register because "
			"of version mismatch core = %d.%d, fsal = %d.%d", name,
			FSAL_MAJOR_VERSION, FSAL_MINOR_VERSION, major_version,
			minor_version);
		load_state = error;
		return so_error;
	}
	pthread_mutex_lock(&fsal_lock);
	so_error = 0;
	if (!(load_state == loading || load_state == init)) {
		so_error = EACCES;
		goto errout;
	}
	new_fsal = fsal_hdl;
	if (name != NULL) {
		new_fsal->name = gsh_strdup(name);
		if (new_fsal->name == NULL) {
			so_error = ENOMEM;
			goto errout;
		}
	}
	fsal_hdl->id = next_fsal_id++;

/* allocate and init ops vector to system wide defaults
 * from FSAL/default_methods.c
 */
	fsal_hdl->ops = gsh_malloc(sizeof(struct fsal_ops));
	if (fsal_hdl->ops == NULL) {
		so_error = ENOMEM;
		goto errout;
	}
	memcpy(fsal_hdl->ops, &def_fsal_ops, sizeof(struct fsal_ops));

	pthread_mutexattr_init(&attrs);
#if defined(__linux__)
	pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_ADAPTIVE_NP);
#endif
	pthread_mutex_init(&fsal_hdl->lock, &attrs);
	glist_init(&fsal_hdl->exports);
	avltree_insert(&fsal_hdl->by_name, &fsal_by_name);
	avltree_insert(&fsal_hdl->by_num, &fsal_by_num);
	if (load_state == loading)
		load_state = registered;
	pthread_mutex_unlock(&fsal_lock);
	return 0;

 errout:
	if (fsal_hdl->path)
		gsh_free(fsal_hdl->path);
	if (fsal_hdl->name)
		gsh_free((char *)fsal_hdl->name);
	if (fsal_hdl->ops)
		gsh_free(fsal_hdl->ops);
	load_state = error;
	pthread_mutex_unlock(&fsal_lock);
	LogCrit(COMPONENT_INIT, "FSAL \"%s\" failed to register because: %s",
		name, strerror(so_error));
	return so_error;
}

/**
 * @brief Unregisterx an FSAL
 *
 * Verify that the fsal is not busy and release all its resources
 * owned at this level.  Mutex is already freed.  Called from the
 * module's MODULE_FINI
 *
 * @param[in] fsal_hdl FSAL handle
 *
 * @retval 0 on success.
 * @retval EBUSY if FSAL is in use.
 */

int unregister_fsal(struct fsal_module *fsal_hdl)
{
	int retval = EBUSY;

	if (fsal_hdl->refs != 0) {	/* this would be very bad */
		goto out;
	}
	if (fsal_hdl->path)
		gsh_free(fsal_hdl->path);
	if (fsal_hdl->name)
		gsh_free((char *)fsal_hdl->name);
	if (fsal_hdl->ops)
		gsh_free(fsal_hdl->ops);
	retval = 0;
 out:
	return retval;
}

/** @} */
