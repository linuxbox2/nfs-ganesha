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


#include "conf_url.h"
#include "conf_url_rados.h"

#ifdef RADOS_URL_PROVIDER

static void cu_rados_url_init(void)
{
	/* XXX do it */
}

static void cu_rados_url_shutdown(void)
{
	/* XXX do it */
}

static int cu_rados_url_fetch(const char *url, FILE **f)
{
	/* XXX do it */
}

static struct gsh_url_provider rados_url_provider = {
	.name = "rados",
	.url_init = cu_rados_url_init,
	.url_shutdown = cu_rados_url_shutdown,
	.url_fetch = cu_rados_url_fetch,
}

void conf_url_rados_pkginit(void)
{
	register_url_provider(&rados_url_provider);
}

#endif /* RADOS_URL_PROVIDER */
