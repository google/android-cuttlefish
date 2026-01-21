#if !defined(__FreeBSD__) && !defined(__NetBSD__) && !defined(__OpenBSD__)
#define _XOPEN_SOURCE 600
#define _DARWIN_C_SOURCE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif
#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#include <f2fs_fs.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#include <sys/stat.h>
#ifdef HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif
#include <libgen.h>
#ifdef HAVE_LINUX_HDREG_H
#include <linux/hdreg.h>
#endif
#ifdef HAVE_LINUX_TYPES_H
#include <linux/types.h>
#endif
#ifdef __KERNEL__
#include <linux/fs.h>
#endif
#include <inttypes.h>

#ifndef FIBMAP
#define FIBMAP          _IO(0x00, 1)    /* bmap access */
#endif

struct file_ext {
	__u64 f_pos;
	__u32 start_blk;
	__u32 end_blk;
	__u32 blk_count;
};

void print_ext(struct file_ext *ext)
{
	if (ext->end_blk == 0)
		printf("%8llu    %8d    %8d    %8d\n", ext->f_pos, 0, 0, ext->blk_count);
	else
		printf("%8llu    %8d    %8d    %8d\n", ext->f_pos, ext->start_blk,
					ext->end_blk, ext->blk_count);
}

#if defined(HAVE_FSTAT64) && !defined(__OSX_AVAILABLE_BUT_DEPRECATED)
void print_stat(struct stat64 *st)
#else
void print_stat(struct stat *st)
#endif
{
	printf("--------------------------------------------\n");
	printf("dev       [%d:%d]\n", major(st->st_dev), minor(st->st_dev));
	printf("ino       [0x%8"PRIx64" : %"PRIu64"]\n",
						st->st_ino, st->st_ino);
	printf("mode      [0x%8x : %d]\n", st->st_mode, st->st_mode);
	printf("nlink     [0x%8lx : %ld]\n",
					(unsigned long)st->st_nlink,
					(long)st->st_nlink);
	printf("uid       [0x%8x : %d]\n", st->st_uid, st->st_uid);
	printf("gid       [0x%8x : %d]\n", st->st_gid, st->st_gid);
	printf("size      [0x%8"PRIx64" : %"PRIu64"]\n",
					(u64)st->st_size, (u64)st->st_size);
	printf("blksize   [0x%8lx : %ld]\n",
					(unsigned long)st->st_blksize,
					(long)st->st_blksize);
	printf("blocks    [0x%8"PRIx64" : %"PRIu64"]\n",
					(u64)st->st_blocks, (u64)st->st_blocks);
	printf("--------------------------------------------\n\n");
}

#if defined(HAVE_FSTAT64) && !defined(__OSX_AVAILABLE_BUT_DEPRECATED)
static void stat_bdev(struct stat64 *st, unsigned int *start_lba)
#else
static void stat_bdev(struct stat *st, unsigned int *start_lba)
#endif
{
	struct stat bdev_stat;
#ifdef HDIO_GETGIO
	struct hd_geometry geom;
#endif
	char devname[32] = { 0, };
	char linkname[32] = { 0, };
	int fd;

	sprintf(devname, "/sys/dev/block/%d:%d", major(st->st_dev), minor(st->st_dev));

	fd = open(devname, O_RDONLY);
	if (fd < 0)
		return;

	if (fstat(fd, &bdev_stat) < 0)
		goto out;

	if (S_ISBLK(bdev_stat.st_mode)) {
#ifdef HDIO_GETGIO
		if (ioctl(fd, HDIO_GETGEO, &geom) < 0)
			*start_lba = 0;
		else
			*start_lba = geom.start;
#else
		*start_lba = 0;
#endif
	}

	if (readlink(devname, linkname, sizeof(linkname)) < 0)
		goto out;

	printf("----------------bdev info-------------------\n");
	printf("devname = %s\n", basename(linkname));
	printf("start_lba = %u\n", *start_lba);

out:
	close(fd);

}

int main(int argc, char *argv[])
{
	int fd;
	int ret = 0;
	char *filename;
#if defined(HAVE_FSTAT64) && !defined(__OSX_AVAILABLE_BUT_DEPRECATED)
	struct stat64 st;
#else
	struct stat st;
#endif
	int total_blks;
	unsigned int i;
	struct file_ext ext;
	__u32 start_lba;
	__u32 blknum;

	if (argc != 2) {
		fprintf(stderr, "No filename\n");
		exit(-1);
	}
	filename = argv[1];

	fd = open(filename, O_RDONLY|O_LARGEFILE);
	if (fd < 0) {
		ret = errno;
		perror(filename);
		exit(-1);
	}

	fsync(fd);

#if defined(HAVE_FSTAT64) && !defined(__OSX_AVAILABLE_BUT_DEPRECATED)
	if (fstat64(fd, &st) < 0) {
#else
	if (fstat(fd, &st) < 0) {
#endif
		ret = errno;
		perror(filename);
		goto out;
	}

	stat_bdev(&st, &start_lba);

	total_blks = (st.st_size + st.st_blksize - 1) / st.st_blksize;

	printf("\n----------------file info-------------------\n");
	printf("%s :\n", filename);
	print_stat(&st);
	printf("file_pos   start_blk     end_blk        blks\n");

	blknum = 0;
	if (ioctl(fd, FIBMAP, &blknum) < 0) {
		ret = errno;
		perror("ioctl(FIBMAP)");
		goto out;
	}
	ext.f_pos = 0;
	ext.start_blk = blknum;
	ext.end_blk = blknum;
	ext.blk_count = 1;

	for (i = 1; i < total_blks; i++) {
		blknum = i;

		if (ioctl(fd, FIBMAP, &blknum) < 0) {
			ret = errno;
			perror("ioctl(FIBMAP)");
			goto out;
		}

		if ((blknum == 0 && blknum == ext.end_blk) || (ext.end_blk + 1) == blknum) {
			ext.end_blk = blknum;
			ext.blk_count++;
		} else {
			print_ext(&ext);
			ext.f_pos = (__u64)i * st.st_blksize;
			ext.start_blk = blknum;
			ext.end_blk = blknum;
			ext.blk_count = 1;
		}
	}

	print_ext(&ext);
out:
	close(fd);
	return ret;
}
