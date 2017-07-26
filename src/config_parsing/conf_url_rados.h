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

#ifndef CONF_URL_RADOS_H
#define CONF_URL_RADOS_H

#include <stdio.h>
#include "gsh_list.h"

#ifdef RADOS_URL_PROVIDER

void conf_url_rados_pkginit(void);

#endif /* RADOS_URL_PROVIDER */
#endif /* CONF_URL_RADOS_H */
