/**
 * libf2fs.c
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 * Copyright (c) 2019 Google Inc.
 *             http://www.google.com/
 * Copyright (c) 2020 Google Inc.
 *   Robin Hsu <robinhsu@google.com>
 *  : add quick-buffer for sload compression support
 *
 * Dual licensed under the GPL or LGPL version 2 licenses.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif
#include <time.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_LINUX_HDREG_H
#include <linux/hdreg.h>
#endif

#ifndef F_SET_RW_HINT
#define F_LINUX_SPECIFIC_BASE 	1024
#define F_SET_RW_HINT		(F_LINUX_SPECIFIC_BASE + 12)
#endif

#include <stdbool.h>
#include <assert.h>
#include <inttypes.h>
#include "f2fs_fs.h"

struct f2fs_configuration c;

#ifdef HAVE_SPARSE_SPARSE_H
#include <sparse/sparse.h>
struct sparse_file *f2fs_sparse_file;
static char **blocks;
uint64_t blocks_count;
static char *zeroed_block;
#endif

static int __get_device_fd(__u64 *offset)
{
	__u64 blk_addr = *offset >> F2FS_BLKSIZE_BITS;
	int i;

	for (i = 0; i < c.ndevs; i++) {
		if (c.devices[i].start_blkaddr <= blk_addr &&
				c.devices[i].end_blkaddr >= blk_addr) {
			*offset -=
				c.devices[i].start_blkaddr << F2FS_BLKSIZE_BITS;
			return c.devices[i].fd;
		}
	}
	return -1;
}

/* ---------- dev_cache, Least Used First (LUF) policy  ------------------- */
/*
 * Least used block will be the first victim to be replaced when max hash
 * collision exceeds
 */
static bool *dcache_valid; /* is the cached block valid? */
static off_t  *dcache_blk; /* which block it cached */
static uint64_t *dcache_lastused; /* last used ticks for cache entries */
static char *dcache_buf; /* cached block data */
static uint64_t dcache_usetick; /* current use tick */

static uint64_t dcache_raccess;
static uint64_t dcache_rhit;
static uint64_t dcache_rmiss;
static uint64_t dcache_rreplace;

static bool dcache_exit_registered = false;

/*
 *  Shadow config:
 *
 *  Active set of the configurations.
 *  Global configuration 'dcache_config' will be transferred here when
 *  when dcache_init() is called
 */
static dev_cache_config_t dcache_config = {0, 16, 1};
static bool dcache_initialized = false;

#define MIN_NUM_CACHE_ENTRY  1024L
#define MAX_MAX_HASH_COLLISION  16

static long dcache_relocate_offset0[] = {
	20, -20, 40, -40, 80, -80, 160, -160,
	320, -320, 640, -640, 1280, -1280, 2560, -2560,
};
static int dcache_relocate_offset[16];

static void dcache_print_statistics(void)
{
	long i;
	long useCnt;

	/* Number of used cache entries */
	useCnt = 0;
	for (i = 0; i < dcache_config.num_cache_entry; i++)
		if (dcache_valid[i])
			++useCnt;

	/*
	 *  c: number of cache entries
	 *  u: used entries
	 *  RA: number of read access blocks
	 *  CH: cache hit
	 *  CM: cache miss
	 *  Repl: read cache replaced
	 */
	printf ("\nc, u, RA, CH, CM, Repl=\n");
	printf ("%ld %ld %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
			dcache_config.num_cache_entry,
			useCnt,
			dcache_raccess,
			dcache_rhit,
			dcache_rmiss,
			dcache_rreplace);
}

void dcache_release(void)
{
	if (!dcache_initialized)
		return;

	dcache_initialized = false;

	if (c.cache_config.dbg_en)
		dcache_print_statistics();

	if (dcache_blk != NULL)
		free(dcache_blk);
	if (dcache_lastused != NULL)
		free(dcache_lastused);
	if (dcache_buf != NULL)
		free(dcache_buf);
	if (dcache_valid != NULL)
		free(dcache_valid);
	dcache_config.num_cache_entry = 0;
	dcache_blk = NULL;
	dcache_lastused = NULL;
	dcache_buf = NULL;
	dcache_valid = NULL;
}

// return 0 for success, error code for failure.
static int dcache_alloc_all(long n)
{
	if (n <= 0)
		return -1;
	if ((dcache_blk = (off_t *) malloc(sizeof(off_t) * n)) == NULL
		|| (dcache_lastused = (uint64_t *)
				malloc(sizeof(uint64_t) * n)) == NULL
		|| (dcache_buf = (char *) malloc (F2FS_BLKSIZE * n)) == NULL
		|| (dcache_valid = (bool *) calloc(sizeof(bool) * n, 1)) == NULL)
	{
		dcache_release();
		return -1;
	}
	dcache_config.num_cache_entry = n;
	return 0;
}

static void dcache_relocate_init(void)
{
	int i;
	int n0 = (sizeof(dcache_relocate_offset0)
			/ sizeof(dcache_relocate_offset0[0]));
	int n = (sizeof(dcache_relocate_offset)
			/ sizeof(dcache_relocate_offset[0]));

	ASSERT(n == n0);
	for (i = 0; i < n && i < dcache_config.max_hash_collision; i++) {
		if (labs(dcache_relocate_offset0[i])
				> dcache_config.num_cache_entry / 2) {
			dcache_config.max_hash_collision = i;
			break;
		}
		dcache_relocate_offset[i] =
				dcache_config.num_cache_entry
				+ dcache_relocate_offset0[i];
	}
}

void dcache_init(void)
{
	long n;

	if (c.cache_config.num_cache_entry <= 0)
		return;

	/* release previous cache init, if any */
	dcache_release();

	dcache_blk = NULL;
	dcache_lastused = NULL;
	dcache_buf = NULL;
	dcache_valid = NULL;

	dcache_config = c.cache_config;

	n = max(MIN_NUM_CACHE_ENTRY, dcache_config.num_cache_entry);

	/* halve alloc size until alloc succeed, or min cache reached */
	while (dcache_alloc_all(n) != 0 && n !=  MIN_NUM_CACHE_ENTRY)
		n = max(MIN_NUM_CACHE_ENTRY, n/2);

	/* must be the last: data dependent on num_cache_entry */
	dcache_relocate_init();
	dcache_initialized = true;

	if (!dcache_exit_registered) {
		dcache_exit_registered = true;
		atexit(dcache_release); /* auto release */
	}

	dcache_raccess = 0;
	dcache_rhit = 0;
	dcache_rmiss = 0;
	dcache_rreplace = 0;
}

static inline char *dcache_addr(long entry)
{
	return dcache_buf + F2FS_BLKSIZE * entry;
}

/* relocate on (n+1)-th collision */
static inline long dcache_relocate(long entry, int n)
{
	assert(dcache_config.num_cache_entry != 0);
	return (entry + dcache_relocate_offset[n]) %
			dcache_config.num_cache_entry;
}

static long dcache_find(__u64 blk)
{
	register long n = dcache_config.num_cache_entry;
	register unsigned m = dcache_config.max_hash_collision;
	long entry, least_used, target;
	unsigned try;

	assert(n > 0);
	target = least_used = entry = blk % n; /* simple modulo hash */

	for (try = 0; try < m; try++) {
		if (!dcache_valid[target] || dcache_blk[target] == blk)
			return target;  /* found target or empty cache slot */
		if (dcache_lastused[target] < dcache_lastused[least_used])
			least_used = target;
		target = dcache_relocate(entry, try); /* next target */
	}
	return least_used;  /* max search reached, return least used slot */
}

/* Physical read into cache */
static int dcache_io_read(long entry, __u64 offset, off_t blk)
{
	int fd = __get_device_fd(&offset);

	if (fd < 0)
		return fd;

#ifdef HAVE_PREAD
	if (pread(fd, dcache_buf + entry * F2FS_BLKSIZE, F2FS_BLKSIZE, offset) < 0) {
		MSG(0, "\n pread() fail.\n");
		return -1;
	}
#else
	if (lseek(fd, offset, SEEK_SET) < 0) {
		MSG(0, "\n lseek fail.\n");
		return -1;
	}
	if (read(fd, dcache_buf + entry * F2FS_BLKSIZE, F2FS_BLKSIZE) < 0) {
		MSG(0, "\n read() fail.\n");
		return -1;
	}
#endif
	dcache_lastused[entry] = ++dcache_usetick;
	dcache_valid[entry] = true;
	dcache_blk[entry] = blk;
	return 0;
}

/*
 *  - Note: Read/Write are not symmetric:
 *       For read, we need to do it block by block, due to the cache nature:
 *           some blocks may be cached, and others don't.
 *       For write, since we always do a write-thru, we can join all writes into one,
 *       and write it once at the caller.  This function updates the cache for write, but
 *       not the do a physical write.  The caller is responsible for the physical write.
 *  - Note: We concentrate read/write together, due to the fact of similar structure to find
 *          the relavant cache entries
 *  - Return values:
 *       0: success
 *       1: cache not available (uninitialized)
 *      -1: error
 */
static int dcache_update_rw(void *buf, __u64 offset,
		size_t byte_count, bool is_write)
{
	__u64 blk, start;
	int addr_in_blk;

	if (!dcache_initialized)
		dcache_init(); /* auto initialize */

	if (!dcache_initialized)
		return 1; /* not available */

	blk = offset / F2FS_BLKSIZE;
	addr_in_blk = offset % F2FS_BLKSIZE;
	start = blk * F2FS_BLKSIZE;

	while (byte_count != 0) {
		size_t cur_size = min(byte_count,
				(size_t)(F2FS_BLKSIZE - addr_in_blk));
		long entry = dcache_find(blk);

		if (!is_write)
			++dcache_raccess;

		if (dcache_valid[entry] && dcache_blk[entry] == blk) {
			/* cache hit */
			if (is_write)  /* write: update cache */
				memcpy(dcache_addr(entry) + addr_in_blk,
					buf, cur_size);
			else
				++dcache_rhit;
		} else {
			/* cache miss */
			if (!is_write) {
				int err;
				++dcache_rmiss;
				if (dcache_valid[entry])
					++dcache_rreplace;
				/* read: physical I/O read into cache */
				err = dcache_io_read(entry, start, blk);
				if (err)
					return err;
			}
		}

		/* read: copy data from cache */
		/* write: nothing to do, since we don't do physical write. */
		if (!is_write)
			memcpy(buf, dcache_addr(entry) + addr_in_blk,
				cur_size);

		/* next block */
		++blk;
		buf += cur_size;
		start += F2FS_BLKSIZE;
		byte_count -= cur_size;
		addr_in_blk = 0;
	}
	return 0;
}

/*
 * dcache_update_cache() just update cache, won't do physical I/O.
 * Thus even no error, we need normal non-cache I/O for actual write
 *
 * return value: 1: cache not available
 *               0: success, -1: I/O error
 */
int dcache_update_cache(void *buf, __u64 offset, size_t count)
{
	return dcache_update_rw(buf, offset, count, true);
}

/* handles read into cache + read into buffer  */
int dcache_read(void *buf, __u64 offset, size_t count)
{
	return dcache_update_rw(buf, offset, count, false);
}

/*
 * IO interfaces
 */
int dev_read_version(void *buf, __u64 offset, size_t len)
{
	if (c.sparse_mode)
		return 0;
#ifdef HAVE_RPEAD
	if (pread(c.kd, buf, len, (off_t)offset) < 0)
		return -1;
#else
	if (lseek(c.kd, (off_t)offset, SEEK_SET) < 0)
		return -1;
	if (read(c.kd, buf, len) < 0)
		return -1;
#endif
	return 0;
}

#ifdef HAVE_SPARSE_SPARSE_H
static int sparse_read_blk(__u64 block, int count, void *buf)
{
	int i;
	char *out = buf;
	__u64 cur_block;

	for (i = 0; i < count; ++i) {
		cur_block = block + i;
		if (blocks[cur_block])
			memcpy(out + (i * F2FS_BLKSIZE),
					blocks[cur_block], F2FS_BLKSIZE);
		else if (blocks)
			memset(out + (i * F2FS_BLKSIZE), 0, F2FS_BLKSIZE);
	}
	return 0;
}

static int sparse_write_blk(__u64 block, int count, const void *buf)
{
	int i;
	__u64 cur_block;
	const char *in = buf;

	for (i = 0; i < count; ++i) {
		cur_block = block + i;
		if (blocks[cur_block] == zeroed_block)
			blocks[cur_block] = NULL;
		if (!blocks[cur_block]) {
			blocks[cur_block] = calloc(1, F2FS_BLKSIZE);
			if (!blocks[cur_block])
				return -ENOMEM;
		}
		memcpy(blocks[cur_block], in + (i * F2FS_BLKSIZE),
				F2FS_BLKSIZE);
	}
	return 0;
}

static int sparse_write_zeroed_blk(__u64 block, int count)
{
	int i;
	__u64 cur_block;

	for (i = 0; i < count; ++i) {
		cur_block = block + i;
		if (blocks[cur_block])
			continue;
		blocks[cur_block] = zeroed_block;
	}
	return 0;
}

#ifdef SPARSE_CALLBACK_USES_SIZE_T
static int sparse_import_segment(void *UNUSED(priv), const void *data,
		size_t len, unsigned int block, unsigned int nr_blocks)
#else
static int sparse_import_segment(void *UNUSED(priv), const void *data, int len,
		unsigned int block, unsigned int nr_blocks)
#endif
{
	/* Ignore chunk headers, only write the data */
	if (!nr_blocks || len % F2FS_BLKSIZE)
		return 0;

	return sparse_write_blk(block, nr_blocks, data);
}

static int sparse_merge_blocks(uint64_t start, uint64_t num, int zero)
{
	char *buf;
	uint64_t i;

	if (zero) {
		blocks[start] = NULL;
		return sparse_file_add_fill(f2fs_sparse_file, 0x0,
					F2FS_BLKSIZE * num, start);
	}

	buf = calloc(num, F2FS_BLKSIZE);
	if (!buf) {
		fprintf(stderr, "failed to alloc %llu\n",
			(unsigned long long)num * F2FS_BLKSIZE);
		return -ENOMEM;
	}

	for (i = 0; i < num; i++) {
		memcpy(buf + i * F2FS_BLKSIZE, blocks[start + i], F2FS_BLKSIZE);
		free(blocks[start + i]);
		blocks[start + i] = NULL;
	}

	/* free_sparse_blocks will release this buf. */
	blocks[start] = buf;

	return sparse_file_add_data(f2fs_sparse_file, blocks[start],
					F2FS_BLKSIZE * num, start);
}
#else
static int sparse_read_blk(__u64 UNUSED(block),
				int UNUSED(count), void *UNUSED(buf))
{
	return 0;
}

static int sparse_write_blk(__u64 UNUSED(block),
				int UNUSED(count), const void *UNUSED(buf))
{
	return 0;
}

static int sparse_write_zeroed_blk(__u64 UNUSED(block), int UNUSED(count))
{
	return 0;
}
#endif

int dev_read(void *buf, __u64 offset, size_t len)
{
	int fd;
	int err;

	if (c.sparse_mode)
		return sparse_read_blk(offset / F2FS_BLKSIZE,
					len / F2FS_BLKSIZE, buf);

	/* err = 1: cache not available, fall back to non-cache R/W */
	/* err = 0: success, err=-1: I/O error */
	err = dcache_read(buf, offset, len);
	if (err <= 0)
		return err;

	fd = __get_device_fd(&offset);
	if (fd < 0)
		return fd;
#ifdef HAVE_PREAD
	if (pread(fd, buf, len, (off_t)offset) < 0)
		return -1;
#else
	if (lseek(fd, (off_t)offset, SEEK_SET) < 0)
		return -1;
	if (read(fd, buf, len) < 0)
		return -1;
#endif
	return 0;
}

#ifdef POSIX_FADV_WILLNEED
int dev_readahead(__u64 offset, size_t len)
#else
int dev_readahead(__u64 offset, size_t UNUSED(len))
#endif
{
	int fd = __get_device_fd(&offset);

	if (fd < 0)
		return fd;
#ifdef POSIX_FADV_WILLNEED
	return posix_fadvise(fd, offset, len, POSIX_FADV_WILLNEED);
#else
	return 0;
#endif
}
/*
 * Copied from fs/f2fs/segment.c
 */
/*
 * This returns write hints for each segment type. This hints will be
 * passed down to block layer as below by default.
 *
 * User                  F2FS                     Block
 * ----                  ----                     -----
 *                       META                     WRITE_LIFE_NONE|REQ_META
 *                       HOT_NODE                 WRITE_LIFE_NONE
 *                       WARM_NODE                WRITE_LIFE_MEDIUM
 *                       COLD_NODE                WRITE_LIFE_LONG
 * ioctl(COLD)           COLD_DATA                WRITE_LIFE_EXTREME
 * extension list        "                        "
 *
 * -- buffered io
 *                       COLD_DATA                WRITE_LIFE_EXTREME
 *                       HOT_DATA                 WRITE_LIFE_SHORT
 *                       WARM_DATA                WRITE_LIFE_NOT_SET
 *
 * -- direct io
 * WRITE_LIFE_EXTREME    COLD_DATA                WRITE_LIFE_EXTREME
 * WRITE_LIFE_SHORT      HOT_DATA                 WRITE_LIFE_SHORT
 * WRITE_LIFE_NOT_SET    WARM_DATA                WRITE_LIFE_NOT_SET
 * WRITE_LIFE_NONE       "                        WRITE_LIFE_NONE
 * WRITE_LIFE_MEDIUM     "                        WRITE_LIFE_MEDIUM
 * WRITE_LIFE_LONG       "                        WRITE_LIFE_LONG
 */
enum rw_hint f2fs_io_type_to_rw_hint(int seg_type)
{
	switch (seg_type) {
	case CURSEG_WARM_DATA:
		return WRITE_LIFE_NOT_SET;
	case CURSEG_HOT_DATA:
		return WRITE_LIFE_SHORT;
	case CURSEG_COLD_DATA:
		return WRITE_LIFE_EXTREME;
	case CURSEG_WARM_NODE:
		return WRITE_LIFE_MEDIUM;
	case CURSEG_HOT_NODE:
		return WRITE_LIFE_NONE;
	case CURSEG_COLD_NODE:
		return WRITE_LIFE_LONG;
	default:
		return WRITE_LIFE_NONE;
	}
}

static int __dev_write(void *buf, __u64 offset, size_t len, enum rw_hint whint)
{
	int fd;

	fd = __get_device_fd(&offset);
	if (fd < 0)
		return fd;

#if ! defined(__MINGW32__)
	if (c.need_whint && (c.whint != whint)) {
		u64 hint = whint;
		int ret;

		ret = fcntl(fd, F_SET_RW_HINT, &hint);
		if (ret != -1)
			c.whint = whint;
	}
#endif

#ifdef HAVE_PWRITE
	if (pwrite(fd, buf, len, (off_t)offset) < 0)
		return -1;
#else
	if (lseek(fd, (off_t)offset, SEEK_SET) < 0)
		return -1;
	if (write(fd, buf, len) < 0)
		return -1;
#endif

	c.need_fsync = true;

	return 0;
}

int dev_write(void *buf, __u64 offset, size_t len, enum rw_hint whint)
{
	if (c.dry_run)
		return 0;

	if (c.sparse_mode)
		return sparse_write_blk(offset / F2FS_BLKSIZE,
					len / F2FS_BLKSIZE, buf);

	/*
	 * dcache_update_cache() just update cache, won't do I/O.
	 * Thus even no error, we need normal non-cache I/O for actual write
	 */
	if (dcache_update_cache(buf, offset, len) < 0)
		return -1;

	return __dev_write(buf, offset, len, whint);
}

int dev_write_block(void *buf, __u64 blk_addr, enum rw_hint whint)
{
	return dev_write(buf, blk_addr << F2FS_BLKSIZE_BITS, F2FS_BLKSIZE, whint);
}

int dev_write_dump(void *buf, __u64 offset, size_t len)
{
#ifdef HAVE_PWRITE
	if (pwrite(c.dump_fd, buf, len, (off_t)offset) < 0)
		return -1;
#else
	if (lseek(c.dump_fd, (off_t)offset, SEEK_SET) < 0)
		return -1;
	if (write(c.dump_fd, buf, len) < 0)
		return -1;
#endif
	return 0;
}

#if !defined(__MINGW32__)
int dev_write_symlink(char *buf, size_t len)
{
	buf[len] = 0;
	if (symlink(buf, c.dump_symlink))
		return -1;
	return 0;
}
#endif

int dev_fill(void *buf, __u64 offset, size_t len, enum rw_hint whint)
{
	if (c.sparse_mode)
		return sparse_write_zeroed_blk(offset / F2FS_BLKSIZE,
						len / F2FS_BLKSIZE);

	/* Only allow fill to zero */
	if (*((__u8*)buf))
		return -1;

	return __dev_write(buf, offset, len, whint);
}

int dev_fill_block(void *buf, __u64 blk_addr, enum rw_hint whint)
{
	return dev_fill(buf, blk_addr << F2FS_BLKSIZE_BITS, F2FS_BLKSIZE, whint);
}

int dev_read_block(void *buf, __u64 blk_addr)
{
	return dev_read(buf, blk_addr << F2FS_BLKSIZE_BITS, F2FS_BLKSIZE);
}

int dev_reada_block(__u64 blk_addr)
{
	return dev_readahead(blk_addr << F2FS_BLKSIZE_BITS, F2FS_BLKSIZE);
}

int f2fs_fsync_device(void)
{
#ifdef HAVE_FSYNC
	int i;

	if (!c.need_fsync)
		return 0;

	for (i = 0; i < c.ndevs; i++) {
		if (fsync(c.devices[i].fd) < 0) {
			MSG(0, "\tError: Could not conduct fsync!!!\n");
			return -1;
		}
	}
#endif
	return 0;
}

int f2fs_init_sparse_file(void)
{
#ifdef HAVE_SPARSE_SPARSE_H
	if (c.func == MKFS) {
		f2fs_sparse_file = sparse_file_new(F2FS_BLKSIZE, c.device_size);
		if (!f2fs_sparse_file)
			return -1;
	} else {
		f2fs_sparse_file = sparse_file_import(c.devices[0].fd,
							true, false);
		if (!f2fs_sparse_file)
			return -1;

		c.blksize = sparse_file_block_size(f2fs_sparse_file);
		c.blksize_bits = log_base_2(c.blksize);
		if (c.blksize_bits == -1) {
			MSG(0, "\tError: Sparse file blocksize not a power of 2.\n");
			return -1;
		}

		c.device_size = sparse_file_len(f2fs_sparse_file, 0, 0);
		c.device_size &= (~((uint64_t)(F2FS_BLKSIZE - 1)));
	}

	blocks_count = c.device_size / F2FS_BLKSIZE;
	blocks = calloc(blocks_count, sizeof(char *));
	if (!blocks) {
		MSG(0, "\tError: Calloc Failed for blocks!!!\n");
		return -1;
	}

	zeroed_block = calloc(1, F2FS_BLKSIZE);
	if (!zeroed_block) {
		MSG(0, "\tError: Calloc Failed for zeroed block!!!\n");
		return -1;
	}

	return sparse_file_foreach_chunk(f2fs_sparse_file, true, false,
				sparse_import_segment, NULL);
#else
	MSG(0, "\tError: Sparse mode is only supported for android\n");
	return -1;
#endif
}

void f2fs_release_sparse_resource(void)
{
#ifdef HAVE_SPARSE_SPARSE_H
	int j;

	if (c.sparse_mode) {
		if (f2fs_sparse_file != NULL) {
			sparse_file_destroy(f2fs_sparse_file);
			f2fs_sparse_file = NULL;
		}
		for (j = 0; j < blocks_count; j++)
			free(blocks[j]);
		free(blocks);
		blocks = NULL;
		free(zeroed_block);
		zeroed_block = NULL;
	}
#endif
}

#define MAX_CHUNK_SIZE		(1 * 1024 * 1024 * 1024ULL)
#define MAX_CHUNK_COUNT		(MAX_CHUNK_SIZE / F2FS_BLKSIZE)
int f2fs_finalize_device(void)
{
	int i;
	int ret = 0;

#ifdef HAVE_SPARSE_SPARSE_H
	if (c.sparse_mode) {
		int64_t chunk_start = (blocks[0] == NULL) ? -1 : 0;
		uint64_t j;

		if (c.func != MKFS) {
			sparse_file_destroy(f2fs_sparse_file);
			ret = ftruncate(c.devices[0].fd, 0);
			ASSERT(!ret);
			lseek(c.devices[0].fd, 0, SEEK_SET);
			f2fs_sparse_file = sparse_file_new(F2FS_BLKSIZE,
							c.device_size);
		}

		for (j = 0; j < blocks_count; ++j) {
			if (chunk_start != -1) {
				if (j - chunk_start >= MAX_CHUNK_COUNT) {
					ret = sparse_merge_blocks(chunk_start,
							j - chunk_start, 0);
					ASSERT(!ret);
					chunk_start = -1;
				}
			}

			if (chunk_start == -1) {
				if (!blocks[j])
					continue;

				if (blocks[j] == zeroed_block) {
					ret = sparse_merge_blocks(j, 1, 1);
					ASSERT(!ret);
				} else {
					chunk_start = j;
				}
			} else {
				if (blocks[j] && blocks[j] != zeroed_block)
					continue;

				ret = sparse_merge_blocks(chunk_start,
						j - chunk_start, 0);
				ASSERT(!ret);

				if (blocks[j] == zeroed_block) {
					ret = sparse_merge_blocks(j, 1, 1);
					ASSERT(!ret);
				}

				chunk_start = -1;
			}
		}
		if (chunk_start != -1) {
			ret = sparse_merge_blocks(chunk_start,
						blocks_count - chunk_start, 0);
			ASSERT(!ret);
		}

		sparse_file_write(f2fs_sparse_file, c.devices[0].fd,
				/*gzip*/0, /*sparse*/1, /*crc*/0);

		f2fs_release_sparse_resource();
	}
#endif
	/*
	 * We should call fsync() to flush out all the dirty pages
	 * in the block device page cache.
	 */
	for (i = 0; i < c.ndevs; i++) {
#ifdef HAVE_FSYNC
		if (c.need_fsync) {
			ret = fsync(c.devices[i].fd);
			if (ret < 0) {
				MSG(0, "\tError: Could not conduct fsync!!!\n");
				break;
			}
		}
#endif
		ret = close(c.devices[i].fd);
		if (ret < 0) {
			MSG(0, "\tError: Failed to close device file!!!\n");
			break;
		}
		free(c.devices[i].path);
		free(c.devices[i].zone_cap_blocks);
	}
	close(c.kd);

	return ret;
}
