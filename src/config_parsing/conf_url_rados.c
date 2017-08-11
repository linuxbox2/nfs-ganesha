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
#include <stdio.h>
#include <stdbool.h>
#include <regex.h>
#include "log.h"
#include "sal_functions.h"

#ifdef RADOS_URLS

static regex_t url_regex;
static rados_t cluster;
static bool initialized;

static struct rados_url_parameter {
	/** Path to ceph.conf */
	char *ceph_conf;
	/** Userid (?) */
	char *userid;
} rados_url_param;

static struct config_item rados_url_params[] = {
	CONF_ITEM_PATH("ceph_conf", 1, MAXPATHLEN, NULL,
		       rados_url_parameter, ceph_conf),
	CONF_ITEM_STR("userid", 1, MAXPATHLEN, NULL,
		       rados_url_parameter, userid),
	CONFIG_EOL
};

struct config_block rados_url_param_blk = {
	.dbus_interface_name = "org.ganesha.nfsd.config.rados_urls",
	.blk_desc.name = "RADOS_URLS",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = rados_url_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/* decompose RADOS URL into (<pool>/)object
 *
 *  verified to match each of the following:
 *
 *  #define URL1 "my_rados_object"
 *  #define URL2 "mypool_baby/myobject_baby"
 *  #define URL3 "mypool-baby/myobject-baby"
 */

#define RADOS_URL_REGEX \
	"([-a-zA-Z0-9_&=]+)/?([-a-zA-Z0-9_&=/]+)?"

/** @brief url regex initializer
 */
static void init_url_regex(void)
{
	int r;

	r = regcomp(&url_regex, RADOS_URL_REGEX, REG_EXTENDED);
	if (!!r) {
		LogFatal(COMPONENT_INIT,
			"Error initializing rados url regex");
	}
}

static void cu_rados_url_init(void)
{
	int ret;

	ret = rados_create(&cluster, rados_url_param.userid);
	if (ret < 0) {
		LogEvent(COMPONENT_CONFIG, "%s: Failed in rados_create",
			__func__);
		return;
	}

	ret = rados_conf_read_file(cluster, rados_url_param.ceph_conf);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "%s: Failed to read ceph_conf",
			__func__);
		rados_shutdown(cluster);
		return;
	}

	ret = rados_connect(cluster);
	if (ret < 0) {
		LogEvent(COMPONENT_CONFIG, "%s: Failed to connect to cluster",
			__func__);
		rados_shutdown(cluster);
		return;
	}

	init_url_regex();

	initialized = true;
}

static void cu_rados_url_shutdown(void)
{
	if (initialized) {
		rados_shutdown(cluster);
		regfree(&url_regex);
		initialized = false;
	}
}

static inline char *match_dup(regmatch_t *m, char *in)
{
	char *s = NULL;

	if (m->rm_so >= 0) {
		int size;

		size = m->rm_eo - m->rm_so + 1;
		s = (char *)gsh_malloc(size);
		snprintf(s, size, "%s", in + m->rm_so);
	}
	return s;
}

static int cu_rados_url_fetch(const char *url, FILE **f, char **fbuf)
{
	rados_ioctx_t io_ctx;
	char buf[1024], *streambuf;
	char *x0, *x1, *x2;

	char *pool_name;
	char *object_name;

	FILE *stream = NULL;
	regmatch_t match[3];
	size_t streamsz;
	uint64_t off = 0;
	int ret, wrt;

	if (!initialized)
		return -EIO;

	ret = regexec(&url_regex, url, 3, match, 0);
	if (likely(!ret)) {
		/* matched */
		regmatch_t *m = &(match[0]);
		/* matched url pattern is NUL-terminated */
		x0 = match_dup(m, (char *)url);
		printf("match0: %s\n", x0);
		m = &(match[1]);
		x1 = match_dup(m, (char *)url);
		printf("match1: %s\n", x1);
		m = &(match[2]);
		x2 = match_dup(m, (char *)url);
		printf("match2: %s\n", x2);

		if ((!x1) && (!x2))
			goto out;

		if (x1) {
			if (!x2) {
				/* object only */
				pool_name = NULL;
				object_name = x1;
			} else {
				pool_name = x1;
				object_name = x2;
			}
		}
	} else if (ret == REG_NOMATCH) {
		LogWarn(COMPONENT_CONFIG,
			"%s: Failed to match %s as a config URL",
			__func__, url);
		goto out;
	} else {
		char ebuf[100];

		regerror(ret, &url_regex, ebuf, sizeof(ebuf));
		LogWarn(COMPONENT_CONFIG,
			"%s: Error in regexec: %s",
			__func__, ebuf);
		goto out;
	}

	ret = rados_ioctx_create(cluster, pool_name, &io_ctx);
	if (ret < 0) {
		LogEvent(COMPONENT_CONFIG, "%s: Failed to create ioctx",
			__func__);
		cu_rados_url_shutdown();
		goto out;
	}
	do {
		int nread;

		if (!stream) {
			streamsz = 1024;
			stream = open_memstream(&streambuf, &streamsz);
		}
		nread = ret = rados_read(io_ctx, object_name, buf, 1024, off);
		off += nread;
		do {
			wrt = fwrite(buf, nread, 1, stream);
			if (wrt > 0) {
				nread -= wrt;
			}
		} while (wrt > 0);
	} while (ret > 0);

	if (likely(stream)) {
		*f = stream;
		*fbuf = streambuf;
	}

	rados_ioctx_destroy(io_ctx);

out:
	/* allocated or NULL */
	gsh_free(x0);
	gsh_free(x1);
	gsh_free(x2);

	return ret;
}

static struct gsh_url_provider rados_url_provider = {
	.name = "rados",
	.url_init = cu_rados_url_init,
	.url_shutdown = cu_rados_url_shutdown,
	.url_fetch = cu_rados_url_fetch
};

void conf_url_rados_pkginit(void)
{
	register_url_provider(&rados_url_provider);
}

#endif /* RADOS_URLS */
