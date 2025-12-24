/**
 * f2fs_format_utils.c
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * Dual licensed under the GPL or LGPL version 2 licenses.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <f2fs_fs.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>

#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#ifdef HAVE_LINUX_FALLOC_H
#include <linux/falloc.h>
#endif

#ifdef __linux__
#ifndef BLKDISCARD
#define BLKDISCARD	_IO(0x12,119)
#endif
#ifndef BLKSECDISCARD
#define BLKSECDISCARD	_IO(0x12,125)
#endif
#endif

#if defined(FALLOC_FL_PUNCH_HOLE) || defined(BLKDISCARD) || \
	defined(BLKSECDISCARD)
static int trim_device(int i)
{
	unsigned long long range[2];
	struct stat *stat_buf;
	struct device_info *dev = c.devices + i;
	uint64_t bytes = dev->total_sectors * dev->sector_size;
	int fd = dev->fd;

	if (dev->alias_filename) {
		MSG(0, "Info: [%s] Skip Discarding as aliased\n", dev->path);
		return 0;
	}

	stat_buf = malloc(sizeof(struct stat));
	if (stat_buf == NULL) {
		MSG(1, "\tError: Malloc Failed for trim_stat_buf!!!\n");
		return -1;
	}

	if (fstat(fd, stat_buf) < 0 ) {
		MSG(1, "\tError: Failed to get the device stat!!!\n");
		free(stat_buf);
		return -1;
	}

	range[0] = 0;
	range[1] = bytes;

#if defined(WITH_BLKDISCARD) && defined(BLKDISCARD)
	MSG(0, "Info: [%s] Discarding device\n", dev->path);
	if (S_ISREG(stat_buf->st_mode)) {
#if defined(HAVE_FALLOCATE) && defined(FALLOC_FL_PUNCH_HOLE)
		if (fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
				range[0], range[1]) < 0) {
			MSG(0, "Info: fallocate(PUNCH_HOLE|KEEP_SIZE) is failed\n");
		}
#endif
		free(stat_buf);
		return 0;
	} else if (S_ISBLK(stat_buf->st_mode)) {
		if (dev->zoned_model != F2FS_ZONED_NONE) {
			free(stat_buf);
			return f2fs_reset_zones(i);
		}
#ifdef BLKSECDISCARD
		if (ioctl(fd, BLKSECDISCARD, &range) < 0) {
			MSG(0, "Info: This device doesn't support BLKSECDISCARD\n");
		} else {
			MSG(0, "Info: Secure Discarded %lu MB\n",
					(unsigned long)stat_buf->st_size >> 20);
			free(stat_buf);
			return 0;
		}
#endif
		if (ioctl(fd, BLKDISCARD, &range) < 0) {
			MSG(0, "Info: This device doesn't support BLKDISCARD\n");
		} else {
			MSG(0, "Info: Discarded %llu MB\n", range[1] >> 20);
		}
	} else {
		free(stat_buf);
		return -1;
	}
#endif
	free(stat_buf);
	return 0;
}
#else
static int trim_device(int UNUSED(i))
{
	return 0;
}
#endif

#ifdef WITH_ANDROID
static bool is_wiped_device(int i)
{
	struct device_info *dev = c.devices + i;
	int fd = dev->fd;
	char *buf, *zero_buf;
	bool wiped = true;
	int nblocks = (4096 * 4096) / F2FS_BLKSIZE;	/* 16MB size */
	int j;

	/* let's trim the other devices except the first device */
	if (i > 0)
		return false;

	buf = malloc(F2FS_BLKSIZE);
	if (buf == NULL) {
		MSG(1, "\tError: Malloc Failed for buf!!!\n");
		return false;
	}
	zero_buf = calloc(1, F2FS_BLKSIZE);
	if (zero_buf == NULL) {
		MSG(1, "\tError: Calloc Failed for zero buf!!!\n");
		free(buf);
		return false;
	}

	if (lseek(fd, 0, SEEK_SET) < 0) {
		free(zero_buf);
		free(buf);
		return false;
	}

	/* check first n blocks */
	for (j = 0; j < nblocks; j++) {
		if (read(fd, buf, F2FS_BLKSIZE) != F2FS_BLKSIZE ||
				memcmp(buf, zero_buf, F2FS_BLKSIZE)) {
			wiped = false;
			break;
		}
	}
	free(zero_buf);
	free(buf);

	if (wiped)
		MSG(0, "Info: Found all zeros in first %d blocks\n", nblocks);
	return wiped;
}
#else
static bool is_wiped_device(int UNUSED(i))
{
	return false;
}
#endif

int f2fs_trim_devices(void)
{
	int i;

	for (i = 0; i < c.ndevs; i++) {
		if (!is_wiped_device(i) && trim_device(i))
			return -1;
	}
	c.trimmed = 1;
	return 0;
}
