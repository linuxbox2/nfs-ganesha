/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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

/* file.c
 * File I/O methods for VFS module
 */

#include "config.h"

#include <assert.h>
#include "fsal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "FSAL/fsal_commonlib.h"
#include "vfs_methods.h"
#include "fsal_handle_syscalls.h"
#include "gsh_intrinsic.h"
#include "extent.h"

/** vfs_open
 * called with appropriate locks taken at the cache inode level
 */

fsal_status_t vfs_open(struct fsal_obj_handle *obj_hdl,
		       const struct req_op_context *opctx,
		       fsal_openflags_t openflags)
{
	struct vfs_fsal_obj_handle *myself;
	int fd;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
        int posix_flags = 0;
	int retval = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd == -1
	       && myself->u.file.openflags == FSAL_O_CLOSED
	       && openflags != 0);

	fsal2posix_openflags(openflags, &posix_flags);
	LogFullDebug(COMPONENT_FSAL,
		     "open_by_handle_at flags from %x to %x",
		     openflags, posix_flags);
	fd = vfs_fsal_open(myself, posix_flags, &fsal_error);
	if(fd < 0) {
		retval =  -fd;
	} else {
		myself->u.file.fd = fd;
		myself->u.file.openflags = openflags;
	}
	return fsalstat(fsal_error, retval);	
}

/* vfs_status
 * Let the caller peek into the file's open/close state.
 */

fsal_openflags_t vfs_status(struct fsal_obj_handle *obj_hdl)
{
	struct vfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	return myself->u.file.openflags;
}

/* vfs_read
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t vfs_read(struct fsal_obj_handle *obj_hdl,
                       const struct req_op_context *opctx,
		       uint64_t offset,
                       size_t buffer_size,
                       void *buffer,
		       size_t *read_amount,
		       bool *end_of_file)
{
	struct vfs_fsal_obj_handle *myself;
	ssize_t nb_read;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 && myself->u.file.openflags != FSAL_O_CLOSED);

        nb_read = pread(myself->u.file.fd,
                        buffer,
                        buffer_size,
                        offset);

        if(offset == -1 || nb_read == -1) {
                retval = errno;
                fsal_error = posix2fsal_error(retval);
                goto out;
        }

        *read_amount = nb_read;

        /* dual eof condition, cf. GPFS */
        *end_of_file = ((nb_read == 0) /* most clients */ || /* ESXi */
                        (((offset + nb_read) >= obj_hdl->attributes.filesize)))
            ? true : false;

out:
	return fsalstat(fsal_error, retval);
}

/* vfs_write
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t vfs_write(struct fsal_obj_handle *obj_hdl,
                        const struct req_op_context *opctx,
			uint64_t offset,
			size_t buffer_size,
			void *buffer,
			size_t *write_amount,
			bool *fsal_stable)
{
	struct vfs_fsal_obj_handle *myself;
	ssize_t nb_written;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 &&
	       myself->u.file.openflags != FSAL_O_CLOSED);

	fsal_set_credentials(opctx->creds);
        nb_written = pwrite(myself->u.file.fd,
                            buffer,
                            buffer_size,
                            offset);

	if(offset == -1 || nb_written == -1) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
		goto out;
	}

	*write_amount = nb_written;

        /* attempt stability */
        if (*fsal_stable) {
            retval = fsync(myself->u.file.fd);
            if(retval == -1) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
            }
            *fsal_stable = true;
        }

out:
	fsal_restore_ganesha_credentials();
	return fsalstat(fsal_error, retval);	
}

bool check_uio(struct gsh_uio *uio)
{
    int ix;
    struct gsh_iovec *iov;

    for (ix = 0; ix < uio->uio_iovcnt; ++ix)  {
        iov = &uio->uio_iov[ix];

        LogDebug(COMPONENT_FSAL,
                "check_uio "
                "ix=%d "
                "uio_iovcnt=%d uio_offset=%"PRIu64 " "
                "uio_resid=%"PRIu64
                " %s flags=%d "
                "iov_base=%p iov_len=%"PRIu64 " iov_map=%p",
                ix,
                uio->uio_iovcnt, uio->uio_offset, uio->uio_resid,
                (uio->uio_rw == GSH_UIO_READ) ?
                "UIO_READ" : "UIO_WRITE",
                uio->uio_flags,
                iov->iov_base, iov->iov_len, iov->iov_map);
    }
    return (TRUE);
}

/* vfs_uio_rdwr
 */

#define UIO_RDWR_TRACE 1

#if 0
static void check_mapping(struct mapping *map, size_t len)
{
	char *buf = gsh_calloc(1, 8192);
	size_t off;

	for (off = 0; off < len; off += 8192) {
		memcpy(buf, map->addr + off, 8192);
		printf("checked map %p off %"PRIu64, map->addr, off);
	}
}
#endif

fsal_status_t vfs_uio_rdwr(struct fsal_obj_handle *obj_hdl,
                           struct gsh_uio *uio,
			   bool *fsal_stable)
{
	struct vfs_fsal_obj_handle *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	struct opr_rbtree_node *node;
	struct mapping map_k, *map;
	struct gsh_uio tuio = *uio;
	struct gsh_iovec *iov;
	uint64_t off, end;
	int retval = 0;
	int ix = 0;

	hdl = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	assert((hdl->u.file.fd >= 0) &&
	       (hdl->u.file.openflags != FSAL_O_CLOSED));

#if UIO_RDWR_TRACE
	LogDebug(COMPONENT_FSAL,
		 "uio_rdwr enter "
		 "uio_iovcnt=%d uio_offset=%"PRIu64 " "
		 "uio_resid=%"PRIu64
		 " %s flags=%d ",
		 uio->uio_iovcnt, uio->uio_offset, uio->uio_resid,
		 (uio->uio_rw == GSH_UIO_READ) ?
		 "UIO_READ" : "UIO_WRITE",
		 uio->uio_flags);
#endif

	/* on entry, uio_offset indicates logical read or write offset */

	/* end is computed from uio_resid (now a size_t) */
	switch (uio->uio_rw) {
	case GSH_UIO_READ:
		/* XXX assert(up-to-date) */
		end = MIN((uio->uio_offset + uio->uio_resid),
			  (hdl->obj_handle.attributes.filesize));
		break;
	case GSH_UIO_WRITE:
		end = uio->uio_offset + uio->uio_resid;
		if (end > hdl->obj_handle.attributes.filesize) {
			retval = ftruncate(hdl->u.file.fd, end);
			if (retval == 0)
				hdl->obj_handle.attributes.filesize = end;
		}
		break;
	default:
		/* error */
		goto out;
		break;
	}

#if UIO_RDWR_TRACE
	LogDebug(COMPONENT_FSAL,
		 "compute end "
		 "start=%"PRIu64 " end=%"PRIu64 " attrs.fsize=%"PRIu64 " "
		 "uio_iovcnt=%d uio_offset=%"PRIu64 " "
		 "uio_resid=%"PRIu64
		 " %s flags=%d ",
		 tuio.uio_offset, end, hdl->obj_handle.attributes.filesize,
		 uio->uio_iovcnt, uio->uio_offset, uio->uio_resid,
		 (uio->uio_rw == GSH_UIO_READ) ?
		 "UIO_READ" : "UIO_WRITE",
		 uio->uio_flags);
#endif
	if (tuio.uio_offset >= end) {
		uio->uio_iovcnt = 0;
		goto out;
	}

	/* project the required iovec range */
	uio->uio_resid = end - tuio.uio_offset; /* adjusted resid */
	uio->uio_iovcnt =
		vfs_extents_in_range(tuio.uio_offset, uio->uio_resid);
	uio->uio_iov = (struct gsh_iovec *)
		gsh_calloc(uio->uio_iovcnt, sizeof(struct gsh_iovec));

	off = tuio.uio_offset;
	do {
		size_t adj_off;
		map_k.off = vfs_extent_of(off);
		pthread_spin_lock(&hdl->maps.sp);
		node = opr_rbtree_lookup(&hdl->maps.t, &map_k.node_k);
		if (unlikely(node)) {
			map = opr_containerof(node, struct mapping, node_k);
			pthread_spin_lock(&map->sp);
			pthread_spin_unlock(&hdl->maps.sp);
			++(map->refcnt);
			LogDebug(COMPONENT_FSAL,
				 "reuse mapping %p ", map);
		} else {
			/* new mapping */
			map = pool_alloc(extent_pool, NULL);
			pthread_spin_init(&map->sp, PTHREAD_PROCESS_PRIVATE);
			pthread_spin_lock(&map->sp);
			opr_rbtree_insert(&hdl->maps.t, &map->node_k);
			pthread_spin_unlock(&hdl->maps.sp);
			map->refcnt = 2 /* sentinel + 1 */;
			map->off = map_k.off;
			map->len = VFS_MAP_SIZE;
			map->addr = mmap(NULL, VFS_MAP_SIZE, VFS_MAP_PROT,
					 VFS_MAP_FLAGS, hdl->u.file.fd,
					 map->off);
			assert(map->addr != (char *) MAP_FAILED);
			LogDebug(COMPONENT_FSAL,
				 "new mapping %p ", map);
		}
		pthread_spin_unlock(&map->sp);

		iov = &(uio->uio_iov[ix]);
		iov->iov_map = map;

		/* adj. offset */
		if (ix == 0) {
			if (off < VFS_MAP_SIZE)
				adj_off = off;
			else
				adj_off = off % VFS_MAP_SIZE;
		} else
			adj_off = 0;

			iov->iov_base = map->addr + adj_off;
			iov->iov_len = MIN(VFS_MAP_SIZE - adj_off,
					   tuio.uio_resid);
			tuio.uio_resid -= iov->iov_len;

#if UIO_RDWR_TRACE
		LogDebug(COMPONENT_FSAL,
			 "mapped segment ix=%d "
			 "off=%"PRIu64 " end=%"PRIu64 " "
			 "uio_iovcnt=%d uio_offset=%"PRIu64 " "
			 "uio_resid=%"PRIu64
			 " %s flags=%d "
			 "iov_base=%p iov_len=%"PRIu64 " iov_map=%p",
			 ix, off, end,
			 uio->uio_iovcnt, uio->uio_offset, uio->uio_resid,
			 (uio->uio_rw == GSH_UIO_READ) ?
			 "UIO_READ" : "UIO_WRITE",
			 uio->uio_flags,
			 iov->iov_base, iov->iov_len, iov->iov_map);
#endif
		/* advance iov */
		++ix;
	} while ((off = vfs_extent_next(map->off)) <  end);

	/* XXX fix */
	if (! uio->uio_iov[(uio->uio_iovcnt-1)].iov_map)
		uio->uio_iovcnt--;

	/* mark for release */
	uio->uio_flags |= GSH_UIO_RELE;
#if UIO_RDWR_TRACE
	check_uio(uio);
#endif

out:
	LogDebug(COMPONENT_FSAL,
		 "uio_rdwr exit fsal_error %d retval %d "
		 "uio_iovcnt=%d uio_offset=%"PRIu64 " "
		 "uio_resid=%"PRIu64
		 " %s flags=%d ",
		 fsal_error, retval,
		 uio->uio_iovcnt, uio->uio_offset, uio->uio_resid,
		 (uio->uio_rw == GSH_UIO_READ) ?
		 "UIO_READ" : "UIO_WRITE",
		 uio->uio_flags);

	return fsalstat(fsal_error, retval);
}

#include "extent_inline.h"

/* vfs_uio_rele
 */

fsal_status_t vfs_uio_rele(struct fsal_obj_handle *obj_hdl,
                           struct gsh_uio *uio)
{
	struct vfs_fsal_obj_handle *hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	struct mapping *map;
	struct gsh_iovec *iov;
	int retval = 0;
	int ix;

	hdl = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	for (ix = 0; ix < uio->uio_iovcnt; ++ix)  {
		iov = &uio->uio_iov[ix];
		map = iov->iov_map;
		pthread_spin_lock(&map->sp);
		/* decref */
		--(map->refcnt);
		if (map->refcnt == 0 /* sentinel refcnt already released (prune) */) {
			/* release mapping */
			pthread_spin_unlock(&map->sp);
			pthread_spin_lock(&hdl->maps.sp);
			pthread_spin_lock(&map->sp);
			if (map->refcnt == 0) {
				/* not raced */
				retval = vfs_extent_remove_mapping(hdl, map);
				if (unlikely(retval == -1))
					fsal_error = ERR_FSAL_IO;
				iov->iov_map = NULL;
				continue;
			}
			/* raced ftw */
			pthread_spin_unlock(&map->sp);
			pthread_spin_unlock(&hdl->maps.sp);
		}
		pthread_spin_unlock(&map->sp);
	}

	gsh_free(uio->uio_iov);
	uio->uio_iov = NULL;
	uio->uio_iovcnt = 0;

	return fsalstat(fsal_error, retval);
}

/* vfs_commit
 * Commit a file range to storage.
 * for right now, fsync will have to do.
 */

fsal_status_t vfs_commit(struct fsal_obj_handle *obj_hdl, /* sync */
			 off_t offset,
			 size_t len)
{
	struct vfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 &&
	       myself->u.file.openflags != FSAL_O_CLOSED);

	retval = fsync(myself->u.file.fd);
	if(retval == -1) {
		retval = errno;
		fsal_error = posix2fsal_error(retval);
	}
	return fsalstat(fsal_error, retval);	
}

/* vfs_lock_op
 * lock a region of the file
 * throw an error if the fd is not open.  The old fsal didn't
 * check this.
 */

fsal_status_t vfs_lock_op(struct fsal_obj_handle *obj_hdl,
			  const struct req_op_context *opctx,
			  void * p_owner,
			  fsal_lock_op_t lock_op,
			  fsal_lock_param_t *request_lock,
			  fsal_lock_param_t *conflicting_lock)
{
	struct vfs_fsal_obj_handle *myself;
	struct flock lock_args;
	int fcntl_comm;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if(myself->u.file.fd < 0 || myself->u.file.openflags == FSAL_O_CLOSED) {
		LogDebug(COMPONENT_FSAL,
			 "Attempting to lock with no file descriptor open");
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	if(p_owner != NULL) {
		fsal_error = ERR_FSAL_NOTSUPP;
		goto out;
	}
	LogFullDebug(COMPONENT_FSAL,
		     "Locking: op:%d type:%d start:%"PRIu64" length:%lu ",
		     lock_op,
		     request_lock->lock_type,
		     request_lock->lock_start,
		     request_lock->lock_length);
	if(lock_op == FSAL_OP_LOCKT) {
		fcntl_comm = F_GETLK;
	} else if(lock_op == FSAL_OP_LOCK || lock_op == FSAL_OP_UNLOCK) {
		fcntl_comm = F_SETLK;
	} else {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: Lock operation requested was not TEST, READ, or WRITE.");
		fsal_error = ERR_FSAL_NOTSUPP;
		goto out;
	}

	if(request_lock->lock_type == FSAL_LOCK_R) {
		lock_args.l_type = F_RDLCK;
	} else if(request_lock->lock_type == FSAL_LOCK_W) {
		lock_args.l_type = F_WRLCK;
	} else {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: The requested lock type was not read or write.");
		fsal_error = ERR_FSAL_NOTSUPP;
		goto out;
	}

	if(lock_op == FSAL_OP_UNLOCK)
		lock_args.l_type = F_UNLCK;

	lock_args.l_len = request_lock->lock_length;
	lock_args.l_start = request_lock->lock_start;
	lock_args.l_whence = SEEK_SET;

	errno = 0;
	retval = fcntl(myself->u.file.fd, fcntl_comm, &lock_args);
	if(retval && lock_op == FSAL_OP_LOCK) {
		retval = errno;
		if(conflicting_lock != NULL) {
			fcntl_comm = F_GETLK;
			retval = fcntl(myself->u.file.fd,
				       fcntl_comm,
				       &lock_args);
			if(retval) {
				retval = errno; /* we lose the inital error */
				LogCrit(COMPONENT_FSAL,
					"After failing a lock request, I couldn't even"
					" get the details of who owns the lock.");
				fsal_error = posix2fsal_error(retval);
				goto out;
			}
			if(conflicting_lock != NULL) {
				conflicting_lock->lock_length = lock_args.l_len;
				conflicting_lock->lock_start = lock_args.l_start;
				conflicting_lock->lock_type = lock_args.l_type;
			}
		}
		fsal_error = posix2fsal_error(retval);
		goto out;
	}

	/* F_UNLCK is returned then the tested operation would be possible. */
	if(conflicting_lock != NULL) {
		if(lock_op == FSAL_OP_LOCKT && lock_args.l_type != F_UNLCK) {
			conflicting_lock->lock_length = lock_args.l_len;
			conflicting_lock->lock_start = lock_args.l_start;
			conflicting_lock->lock_type = lock_args.l_type;
		} else {
			conflicting_lock->lock_length = 0;
			conflicting_lock->lock_start = 0;
			conflicting_lock->lock_type = FSAL_NO_LOCK;
		}
	}
out:
	return fsalstat(fsal_error, retval);	
}

/* vfs_close
 * Close the file if it is still open.
 * Yes, we ignor lock status.  Closing a file in POSIX
 * releases all locks but that is state and cache inode's problem.
 */

fsal_status_t vfs_close(struct fsal_obj_handle *obj_hdl)
{
	struct vfs_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	assert(obj_hdl->type == REGULAR_FILE);
	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if(myself->u.file.fd >= 0 &&
	   myself->u.file.openflags != FSAL_O_CLOSED) {
		retval = close(myself->u.file.fd);
		if(retval < 0) {
			retval = errno;
			fsal_error = posix2fsal_error(retval);
		}
		myself->u.file.fd = -1;
		myself->u.file.openflags = FSAL_O_CLOSED;
	}
	return fsalstat(fsal_error, retval);	
}

/* vfs_lru_cleanup
 * free non-essential resources at the request of cache inode's
 * LRU processing identifying this handle as stale enough for resource
 * trimming.
 */

fsal_status_t vfs_lru_cleanup(struct fsal_obj_handle *obj_hdl,
                              lru_actions_t flags)
{
    struct vfs_fsal_obj_handle *hdl;
    fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
    int retval = 0;

    if(obj_hdl->type == REGULAR_FILE) {
        hdl = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
        /* lru cleanup is now MQ-aware */
        if (flags & (FSAL_CLEANUP_LRU_WEAK|FSAL_CLEANUP_LRU_L1L2)) {
            /* entry may be referenced, but it has been scanned by
             * lru_thread */
            retval = vfs_extent_prune_extents(hdl);
        }
    }
    return fsalstat(fsal_error, retval);
}
