/* ----------------------------------------------------------------------------
 * Copyright (C) 2017, Red Hat, Inc.
 * contributeur : Matt Benjamin  mbenjamin@redhat.com
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
 * ---------------------------------------
 */

#include "config.h"
#include "log.h"
#include "sal_functions.h"

#include "conf_url.h"

static pthread_rwlock_t url_rwlock = PTHREAD_RWLOCK_INITIALIZER;
static struct glist_head url_providers;

/** @brief register handler for new url type
 */
int register_url_provider(struct gsh_url_provider *nurl_p)
{
	struct gsh_url_provider *url_p;
	struct glist_head *gl;
	int code = 0;

	PTHREAD_RWLOCK_wrlock(&url_rwlock);
	glist_for_each(gl, &url_providers) {
		url_p = glist_entry(gl, struct gsh_url_provider, link);
		if (!strcasecmp(url_p->name, nurl_p->name)) {
			code = EEXIST;
			break;
		}
	}
	nurl_p->url_init();
	glist_add_tail(&url_providers, &nurl_p->link);

	PTHREAD_RWLOCK_unlock(&url_rwlock);
	return code;
}

/** @brief package initializer
 */
void config_url_init(void)
{
	glist_init(&url_providers);

/* init well-known URL providers */
#ifdef RADOS_URL_PROVIDER
	conf_url_rados_pkginit();
#endif
}

/** @brief generic url dispatch
 */
int config_url_fetch(const char *url, FILE **f)
{
	struct gsh_url_provider *url_p;
	struct glist_head *gl;
	int code = EINVAL;

	/* XXX need to match URL! */

	PTHREAD_RWLOCK_rdlock(&url_rwlock);
	glist_for_each(gl, &url_providers) {
		url_p = glist_entry(gl, struct gsh_url_provider, link);
		if (!strcasecmp(url_p->name, url_p->name)) {
			code = url_p->url_fetch(url, f);
			break;
		}
	}
	PTHREAD_RWLOCK_unlock(&url_rwlock);
	return code;
}
