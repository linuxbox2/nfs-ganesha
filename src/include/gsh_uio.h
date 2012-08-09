/*
 * Copyright (C) 2012, Linux Box Corporation
 * All Rights Reserved
 *
 * Contributor: Matt Benjamin
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
 *
 * \file gsh_uio.h
 * \author Matt Benjamin
 * \brief Compiler intrinsics
 *
 * \section DESCRIPTION
 *
 * Emulated, extended Ganesha struct uio.
 *
 */

#ifndef _GSH_UIO_H
#define _GSH_UIO_H

#include <sys/uio.h>

struct gsh_iovec
{
    void *iov_base;
    void *iov_map; /* it's eff. */
    size_t iov_len;
};

enum gsh_uio_rw { GSH_UIO_READ, GSH_UIO_WRITE };

/* Flag values. */
#define GSH_UIO_NONE              0x0000
#define GSH_UIO_EOF               0x0004
#define GSH_UIO_STABLE_DATA       0x0008
#define GSH_UIO_STABLE_METADATA   0x0010
#define GSH_UIO_RDWR              0x0020
#define GSH_UIO_OPENED            0x0040
#define GSH_UIO_CLOSE             0x0080
#define GSH_UIO_NEEDSYNC          0x0100
#define GSH_UIO_LEGACY_IO         0x0200 /* caller supplies a buffer */
#define GSH_UIO_RELE              0x0400

struct gsh_uio {
    struct gsh_iovec *uio_iov;
    void *uio_udata; /* caller private data */
    int uio_iovcnt;
    off_t uio_offset;
    size_t uio_resid;
    uint32_t uio_flags;
    enum gsh_uio_rw uio_rw;
};

#endif /* _GSH_UIO_H */
