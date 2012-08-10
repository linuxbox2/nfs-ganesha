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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "FSAL/fsal_commonlib.h"
#include "vfs_methods.h"
#include "FSAL/FSAL_VFS/fsal_handle_syscalls.h"
#include "gsh_intrinsic.h"
#include "extent.h"

/** vfs_open
 * called with appropriate locks taken at the cache inode level
 */

fsal_status_t vfs_open(struct fsal_obj_handle *obj_hdl,
		       fsal_openflags_t openflags)
{
	struct vfs_fsal_obj_handle *myself;
	int fd, mntfd;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd == -1
	       && myself->u.file.openflags == FSAL_O_CLOSED);

	mntfd = vfs_get_root_fd(obj_hdl->export);
	fd = open_by_handle_at(mntfd, myself->handle, (O_RDWR));
	if(fd < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}
	myself->u.file.fd = fd;
	myself->u.file.openflags = openflags;

out:
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
 */

fsal_status_t vfs_read(struct fsal_obj_handle *obj_hdl,
		       uint64_t offset,
                       size_t buffer_size,
                       void *buffer,
		       size_t *read_amount,
		       bool_t *end_of_file)
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
        *end_of_file = nb_read == 0 ? TRUE : FALSE;
        *read_amount = nb_read;
out:
	return fsalstat(fsal_error, retval);
}

/* vfs_write
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t vfs_write(struct fsal_obj_handle *obj_hdl,
			uint64_t offset,
			size_t buffer_size,
			void *buffer,
			size_t *write_amount)
{
	struct vfs_fsal_obj_handle *myself;
	ssize_t nb_written;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 &&
	       myself->u.file.openflags != FSAL_O_CLOSED);

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
out:
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

fsal_status_t vfs_uio_rdwr(struct fsal_obj_handle *obj_hdl,
                           struct gsh_uio *uio)
{
    struct vfs_fsal_obj_handle *hdl;
    fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
    struct opr_rbtree_node *node;
    struct mapping map_k, *map;
    struct gsh_iovec *iov;
    uint64_t base, end;
    uint32_t l_adj, r_adj = 0;
    int retval = 0;
    int ix = 0;

    hdl = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

    assert((hdl->u.file.fd >= 0) &&
           (hdl->u.file.openflags != FSAL_O_CLOSED));

    LogDebug(COMPONENT_FSAL,
             "uio_rdwr enter "
             "uio_iovcnt=%d uio_offset=%"PRIu64 " "
             "uio_resid=%"PRIu64
             " %s flags=%d ",
             uio->uio_iovcnt, uio->uio_offset, uio->uio_resid,
             (uio->uio_rw == GSH_UIO_READ) ?
             "UIO_READ" : "UIO_WRITE",
             uio->uio_flags);

    /* on entry, uio_offset indicates logical read or write offset */
    base = uio->uio_offset;

    /* end is computed from uio_resid (now a size_t) */
    switch (uio->uio_rw) {
    case GSH_UIO_READ:
        /* XXX assert(up-to-date) */
        end = MIN((uio->uio_offset + uio->uio_resid),
                  (hdl->obj_handle.attributes.filesize));
        break;
    case GSH_UIO_WRITE:
        end = uio->uio_offset + uio->uio_resid;
        retval = ftruncate(hdl->u.file.fd, end);
        break;
    default:
        /* error */
        goto out;
        break;
    }

    /* the following calculation actually uses just the end position,
     * considering the base of the first extent */
    uio->uio_iovcnt = vfs_extents_in_range(base, (end-vfs_extent_of(base)));

    uio->uio_iov = (struct gsh_iovec *)
        gsh_calloc(uio->uio_iovcnt, sizeof(struct gsh_iovec));

    /* will adjust */
    uio->uio_resid = uio->uio_iovcnt * VFS_MAP_SIZE;

    do {
        map_k.off = vfs_extent_of(base);
        pthread_mutex_lock(&hdl->maps.mtx);
        node = opr_rbtree_lookup(&hdl->maps.t, &map_k.node_k);
        if (unlikely(node)) {
            map = opr_containerof(node, struct mapping, node_k);
            pthread_spin_lock(&map->sp);
            pthread_mutex_unlock(&hdl->maps.mtx);
            ++(map->refcnt);
        } else {
            /* new mapping */
            map = pool_alloc(extent_pool, NULL);
            pthread_spin_init(&map->sp, PTHREAD_PROCESS_PRIVATE);
            pthread_spin_lock(&map->sp);
            opr_rbtree_insert(&hdl->maps.t, &map->node_k);
            pthread_mutex_unlock(&hdl->maps.mtx);
            map->refcnt = 2 /* sentinel + 1 */;
            map->off = map_k.off;
            map->len = VFS_MAP_SIZE;
            map->addr = mmap(NULL, VFS_MAP_SIZE, VFS_MAP_PROT, VFS_MAP_FLAGS,
                             hdl->u.file.fd, map->off);
            assert(map->addr != (void *) MAP_FAILED);
        }
        pthread_spin_unlock(&map->sp);

        iov = &(uio->uio_iov[ix]);

        /* left adjust 1st iovec */
        if (ix == 0) {
            l_adj = base % VFS_MAP_SIZE;
            uio->uio_resid -= l_adj;
        } else {
            if (l_adj)
                l_adj = 0;
        }

        /* right adjust last iovec */
        if (ix == (uio->uio_iovcnt-1)) {
            r_adj = VFS_MAP_SIZE - (end % VFS_MAP_SIZE);
            uio->uio_resid -= r_adj;
        }

        iov->iov_base = map->addr + l_adj;
        iov->iov_len = VFS_MAP_SIZE - l_adj - r_adj;
        iov->iov_map = map;

#if UIO_RDWR_TRACE
        if(isDebug(COMPONENT_FSAL))
            LogFullDebug(COMPONENT_FSAL,
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
#endif
        /* advance iov */
        ++ix;
    } while ((base = vfs_extent_next(map->off)) <  end);

    /* mark for release */
    uio->uio_flags |= GSH_UIO_RELE;
    check_uio(uio);

out:
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
            pthread_mutex_lock(&hdl->maps.mtx);
            pthread_spin_lock(&map->sp);
            if (map->refcnt == 0) {
                /* not raced */
                retval = vfs_extent_remove_mapping(hdl, map);
                if (unlikely(retval == -1))
                    fsal_error = ERR_FSAL_IO;
                continue;
            }
            /* raced ftw */
            pthread_spin_unlock(&map->sp);
            pthread_mutex_unlock(&hdl->maps.mtx);
        }
        pthread_spin_unlock(&map->sp);
    }

    gsh_free(uio->uio_iov);
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
	if(conflicting_lock == NULL && lock_op == FSAL_OP_LOCKT) {
		LogDebug(COMPONENT_FSAL,
			 "conflicting_lock argument can't"
			 " be NULL with lock_op  = LOCKT");
		fsal_error = ERR_FSAL_FAULT;
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
	   myself->u.file.openflags != FSAL_O_CLOSED){
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
        if (flags & FSAL_CLEANUP_LRU_WEAK) {
            /* entry may be referenced, but it has been scanned by
             * lru_thread */
            retval = vfs_extent_prune_extents(hdl);
        } else if (flags & FSAL_CLEANUP_LRU_L1L2) {
            /* entry has no references */
            retval = vfs_extent_prune_extents(hdl); /* and no extents */
            /* XXX retval? */
            if(hdl->u.file.fd >= 0) {
                retval = close(hdl->u.file.fd);
                hdl->u.file.fd = -1;
                hdl->u.file.openflags = FSAL_O_CLOSED;
            }
            if(retval == -1) {
                retval = errno;
                fsal_error = posix2fsal_error(retval);
            }
        }
    }
    return fsalstat(fsal_error, retval);
}

