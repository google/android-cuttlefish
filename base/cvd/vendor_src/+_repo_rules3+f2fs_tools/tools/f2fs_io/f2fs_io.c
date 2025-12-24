/*
 * f2fs_io.c - f2fs ioctl utility
 *
 * Author: Jaegeuk Kim <jaegeuk@kernel.org>
 *
 * Copied portion of the code from ../f2fscrypt.c
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#ifndef __SANE_USERSPACE_TYPES__
#define __SANE_USERSPACE_TYPES__       /* For PPC64, to get LL64 types */
#endif

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/fs.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/xattr.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <android_config.h>

#include "f2fs_io.h"

struct cmd_desc {
	const char *cmd_name;
	void (*cmd_func)(int, char **, const struct cmd_desc *);
	const char *cmd_desc;
	const char *cmd_help;
	int cmd_flags;
};

static void __attribute__((noreturn))
do_die(const char *format, va_list va, int err)
{
	vfprintf(stderr, format, va);
	if (err)
		fprintf(stderr, ": %s", strerror(err));
	putc('\n', stderr);
	exit(1);
}

static void __attribute__((noreturn, format(printf, 1, 2)))
die_errno(const char *format, ...)
{
	va_list va;

	va_start(va, format);
	do_die(format, va, errno);
	va_end(va);
}

static void __attribute__((noreturn, format(printf, 1, 2)))
die(const char *format, ...)
{
	va_list va;

	va_start(va, format);
	do_die(format, va, 0);
	va_end(va);
}

static void *xmalloc(size_t size)
{
	void *p = malloc(size);

	if (!p)
		die("Memory alloc failed (requested %zu bytes)", size);
	return p;
}

static void *aligned_xalloc(size_t alignment, size_t size)
{
	void *p = aligned_alloc(alignment, size);

	if (!p)
		die("Memory alloc failed (requested %zu bytes)", size);
	return p;
}

static int xopen(const char *pathname, int flags, mode_t mode)
{
	int fd = open(pathname, flags, mode);

	if (fd < 0)
		die_errno("Failed to open %s", pathname);
	return fd;
}

static ssize_t xread(int fd, void *buf, size_t count)
{
	ssize_t ret = read(fd, buf, count);

	if (ret < 0)
		die_errno("read failed");
	return ret;
}

static void full_write(int fd, const void *buf, size_t count)
{
	while (count) {
		ssize_t ret = write(fd, buf, count);

		if (ret < 0)
			die_errno("write failed");
		buf = (char *)buf + ret;
		count -= ret;
	}
}

#ifdef HAVE_MACH_TIME_H
static u64 get_current_us()
{
	return mach_absolute_time() / 1000;
}
#elif defined(HAVE_CLOCK_GETTIME) && defined(HAVE_CLOCK_BOOTTIME)
static u64 get_current_us()
{
	struct timespec t;
	t.tv_sec = t.tv_nsec = 0;
	clock_gettime(CLOCK_BOOTTIME, &t);
	return (u64)t.tv_sec * 1000000LL + t.tv_nsec / 1000;
}
#else
static u64 get_current_us()
{
	return 0;
}
#endif

#define fsync_desc "fsync"
#define fsync_help						\
"f2fs_io fsync [file]\n\n"					\
"fsync given the file\n"					\

static void do_fsync(int argc, char **argv, const struct cmd_desc *cmd)
{
	int fd;

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[1], O_WRONLY, 0);

	if (fsync(fd) != 0)
		die_errno("fsync failed");

	printf("fsync a file\n");
	exit(0);
}

#define fdatasync_desc "fdatasync"
#define fdatasync_help						\
"f2fs_io fdatasync [file]\n\n"					\
"fdatasync given the file\n"					\

static void do_fdatasync(int argc, char **argv, const struct cmd_desc *cmd)
{
	int fd;

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[1], O_WRONLY, 0);

	if (fdatasync(fd) != 0)
		die_errno("fdatasync failed");

	printf("fdatasync a file\n");
	exit(0);
}

#define set_verity_desc "Set fs-verity"
#define set_verity_help					\
"f2fs_io set_verity [file]\n\n"				\
"Set fsverity bit given a file\n"			\

static void do_set_verity(int argc, char **argv, const struct cmd_desc *cmd)
{
	int ret, fd;
	struct fsverity_enable_arg args = {.version = 1};

	args.hash_algorithm = FS_VERITY_HASH_ALG_SHA256;
	args.block_size = F2FS_DEFAULT_BLKSIZE;

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}
	fd = open(argv[1], O_RDONLY);

	ret = ioctl(fd, FS_IOC_ENABLE_VERITY, &args);
	if (ret < 0) {
		perror("FS_IOC_ENABLE_VERITY");
		exit(1);
	}

	printf("Set fsverity bit to %s\n", argv[1]);
	exit(0);
}

#define getflags_desc "getflags ioctl"
#define getflags_help						\
"f2fs_io getflags [file]\n\n"					\
"get a flag given the file\n"					\
"flag can show \n"						\
"  encryption\n"						\
"  nocow(pinned)\n"						\
"  inline_data\n"						\
"  verity\n"							\
"  casefold\n"							\
"  compression\n"						\
"  nocompression\n"						\
"  immutable\n"

static void do_getflags(int argc, char **argv, const struct cmd_desc *cmd)
{
	long flag = 0;
	int ret, fd;
	int exist = 0;

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[1], O_RDONLY, 0);

	ret = ioctl(fd, F2FS_IOC_GETFLAGS, &flag);
	printf("get a flag on %s ret=%d, flags=", argv[1], ret);
	if (flag & FS_CASEFOLD_FL) {
		printf("casefold");
		exist = 1;
	}
	if (flag & FS_COMPR_FL) {
		if (exist)
			printf(",");
		printf("compression");
		exist = 1;
	}
	if (flag & FS_NOCOMP_FL) {
		if (exist)
			printf(",");
		printf("nocompression");
		exist = 1;
	}
	if (flag & FS_ENCRYPT_FL) {
		if (exist)
			printf(",");
		printf("encrypt");
		exist = 1;
	}
	if (flag & FS_VERITY_FL) {
		if (exist)
			printf(",");
		printf("verity");
		exist = 1;
	}
	if (flag & FS_INLINE_DATA_FL) {
		if (exist)
			printf(",");
		printf("inline_data");
		exist = 1;
	}
	if (flag & FS_NOCOW_FL) {
		if (exist)
			printf(",");
		printf("nocow(pinned)");
		exist = 1;
	}
	if (flag & FS_IMMUTABLE_FL) {
		if (exist)
			printf(",");
		printf("immutable");
		exist = 1;
	}
	if (!exist)
		printf("none");
	printf("\n");
	exit(0);
}

#define setflags_desc "setflags ioctl"
#define setflags_help						\
"f2fs_io setflags [flag] [file]\n\n"				\
"set a flag given the file\n"					\
"flag can be\n"							\
"  casefold\n"							\
"  compression\n"						\
"  nocompression\n"						\
"  immutable\n"							\
"  nocow\n"

static void do_setflags(int argc, char **argv, const struct cmd_desc *cmd)
{
	long flag = 0;
	int ret, fd;

	if (argc != 3) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[2], O_RDONLY, 0);

	ret = ioctl(fd, F2FS_IOC_GETFLAGS, &flag);
	printf("get a flag on %s ret=%d, flags=%lx\n", argv[1], ret, flag);
	if (ret)
		die_errno("F2FS_IOC_GETFLAGS failed");

	if (!strcmp(argv[1], "casefold"))
		flag |= FS_CASEFOLD_FL;
	else if (!strcmp(argv[1], "compression"))
		flag |= FS_COMPR_FL;
	else if (!strcmp(argv[1], "nocompression"))
		flag |= FS_NOCOMP_FL;
	else if (!strcmp(argv[1], "immutable"))
		flag |= FS_IMMUTABLE_FL;
	else if (!strcmp(argv[1], "nocow"))
		flag |= FS_NOCOW_FL;

	ret = ioctl(fd, F2FS_IOC_SETFLAGS, &flag);
	printf("set a flag on %s ret=%d, flags=%s\n", argv[2], ret, argv[1]);
	exit(0);
}

#define clearflags_desc "clearflags ioctl"
#define clearflags_help						\
"f2fs_io clearflags [flag] [file]\n\n"				\
"clear a flag given the file\n"					\
"flag can be\n"							\
"  compression\n"						\
"  nocompression\n"						\
"  immutable\n"							\
"  nocow\n"

static void do_clearflags(int argc, char **argv, const struct cmd_desc *cmd)
{
	long flag = 0;
	int ret, fd;

	if (argc != 3) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[2], O_RDONLY, 0);

	ret = ioctl(fd, F2FS_IOC_GETFLAGS, &flag);
	printf("get a flag on %s ret=%d, flags=%lx\n", argv[1], ret, flag);
	if (ret)
		die_errno("F2FS_IOC_GETFLAGS failed");

	if (!strcmp(argv[1], "compression"))
		flag &= ~FS_COMPR_FL;
	else if (!strcmp(argv[1], "nocompression"))
		flag &= ~FS_NOCOMP_FL;
	else if (!strcmp(argv[1], "immutable"))
		flag &= ~FS_IMMUTABLE_FL;
	else if (!strcmp(argv[1], "nocow"))
		flag &= ~FS_NOCOW_FL;

	ret = ioctl(fd, F2FS_IOC_SETFLAGS, &flag);
	printf("clear a flag on %s ret=%d, flags=%s\n", argv[2], ret, argv[1]);
	exit(0);
}

#define shutdown_desc "shutdown filesystem"
#define shutdown_help					\
"f2fs_io shutdown [level] [dir]\n\n"			\
"Freeze and stop all IOs given mount point\n"		\
"level can be\n"					\
"  0 : going down with full sync\n"			\
"  1 : going down with checkpoint only\n"		\
"  2 : going down with no sync\n"			\
"  3 : going down with metadata flush\n"		\
"  4 : going down with fsck mark\n"

static void do_shutdown(int argc, char **argv, const struct cmd_desc *cmd)
{
	u32 flag;
	int ret, fd;

	if (argc != 3) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	flag = atoi(argv[1]);
	if (flag >= F2FS_GOING_DOWN_MAX) {
		fputs("Wrong level\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}
	fd = xopen(argv[2], O_RDONLY, 0);

	ret = ioctl(fd, F2FS_IOC_SHUTDOWN, &flag);
	if (ret < 0)
		die_errno("F2FS_IOC_SHUTDOWN failed");

	printf("Shutdown %s with level=%d\n", argv[2], flag);
	exit(0);
}

#define fadvise_desc "fadvise"
#define fadvise_help						\
"f2fs_io fadvise [advice] [offset] [length] [file]\n\n"		\
"fadvice given the file\n"					\
"advice can be\n"						\
" willneed\n"							\
" dontneed\n"							\
" noreuse\n"							\
" sequential\n"							\
" random\n"							\

static void do_fadvise(int argc, char **argv, const struct cmd_desc *cmd)
{
	int fd, advice;
	off_t offset, length;

	if (argc != 5) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[4], O_RDWR, 0);

	if (!strcmp(argv[1], "willneed")) {
		advice = POSIX_FADV_WILLNEED;
	} else if (!strcmp(argv[1], "dontneed")) {
		advice = POSIX_FADV_DONTNEED;
	} else if (!strcmp(argv[1], "noreuse")) {
		advice = POSIX_FADV_NOREUSE;
	} else if (!strcmp(argv[1], "sequential")) {
		advice = POSIX_FADV_SEQUENTIAL;
	} else if (!strcmp(argv[1], "random")) {
		advice = POSIX_FADV_RANDOM;
	} else {
		fputs("Wrong advice\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	offset = atoi(argv[2]);
	length = atoll(argv[3]);

	if (posix_fadvise(fd, offset, length, advice) != 0)
		die_errno("fadvise failed");

	printf("fadvice %s to a file: %s\n", argv[1], argv[4]);
	exit(0);
}

#define ioprio_desc "ioprio"
#define ioprio_help						\
"f2fs_io ioprio [hint] [file]\n\n"				\
"ioprio given the file\n"					\
"hint can be\n"							\
" ioprio_write\n"						\

static void do_ioprio(int argc, char **argv, const struct cmd_desc *cmd)
{
	int fd, hint;

	if (argc != 3) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[2], O_RDWR, 0);

	if (!strcmp(argv[1], "ioprio_write")) {
		hint = F2FS_IOPRIO_WRITE;
	} else {
		fputs("Not supported hint\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	if (ioctl(fd, F2FS_IOC_IO_PRIO, &hint) != 0)
		die_errno("ioprio failed");

	printf("ioprio_hint %d to a file: %s\n", hint, argv[2]);
	exit(0);
}

#define pinfile_desc "pin file control"
#define pinfile_help						\
"f2fs_io pinfile [get|set|unset] [file] {size}\n\n"		\
"get/set/unset pinning given the file\n"			\
"{size} is fallocate length and optional only for set operations\n"

static void do_pinfile(int argc, char **argv, const struct cmd_desc *cmd)
{
	u32 pin;
	int ret, fd;

	if (argc < 3 || argc > 4) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[2], O_RDWR, 0);

	ret = -1;
	if (!strcmp(argv[1], "set")) {
		pin = 1;
		ret = ioctl(fd, F2FS_IOC_SET_PIN_FILE, &pin);
		if (ret != 0)
			die_errno("F2FS_IOC_SET_PIN_FILE failed");
		if (argc != 4) {
			printf("%s pinfile: %u blocks moved in %s\n",
						argv[1], ret, argv[2]);
			exit(0);
		}

		struct stat st;
		if (fallocate(fd, 0, 0, atoll(argv[3])) != 0)
			die_errno("fallocate failed");
		if (fstat(fd, &st) != 0)
			die_errno("fstat failed");
		printf("%s pinfile: %u blocks moved and fallocate %"PRIu64" bytes in %s\n",
					argv[1], ret, st.st_size, argv[2]);
	} else if (!strcmp(argv[1], "unset")) {
		pin = 0;
		ret = ioctl(fd, F2FS_IOC_SET_PIN_FILE, &pin);
		if (ret != 0)
			die_errno("F2FS_IOC_SET_PIN_FILE failed");
		printf("%s pinfile in %s\n", argv[1], argv[2]);
	} else if (!strcmp(argv[1], "get")) {
		unsigned int flags;

		ret = ioctl(fd, F2FS_IOC_GET_PIN_FILE, &pin);
		if (ret < 0)
			die_errno("F2FS_IOC_GET_PIN_FILE failed");

		ret = ioctl(fd, F2FS_IOC_GETFLAGS, &flags);
		if (ret < 0)
			die_errno("F2FS_IOC_GETFLAGS failed");

		printf("get_pin_file: %s with %u blocks moved in %s\n",
				(flags & F2FS_NOCOW_FL) ? "pinned" : "un-pinned",
				pin, argv[2]);
	}
	exit(0);
}

#define fallocate_desc "fallocate"
#define fallocate_help						\
"f2fs_io fallocate [-c] [-i] [-p] [-z] [keep_size] [offset] [length] [file]\n\n"	\
"fallocate given the file\n"					\
" [keep_size] : 1 or 0\n"					\
" -c : collapse range\n"					\
" -i : insert range\n"						\
" -p : punch hole\n"						\
" -z : zero range\n"						\

static void do_fallocate(int argc, char **argv, const struct cmd_desc *cmd)
{
	int fd;
	off_t offset, length;
	struct stat sb;
	int mode = 0;
	int c;

	while ((c = getopt(argc, argv, "cipz")) != -1) {
		switch (c) {
		case 'c':
			mode |= FALLOC_FL_COLLAPSE_RANGE;
			break;
		case 'i':
			mode |= FALLOC_FL_INSERT_RANGE;
			break;
		case 'p':
			mode |= FALLOC_FL_PUNCH_HOLE;
			break;
		case 'z':
			mode |= FALLOC_FL_ZERO_RANGE;
			break;
		default:
			fputs(cmd->cmd_help, stderr);
			exit(2);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 4) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	if (!strcmp(argv[0], "1"))
		mode |= FALLOC_FL_KEEP_SIZE;

	offset = atoll(argv[1]);
	length = atoll(argv[2]);

	fd = xopen(argv[3], O_RDWR, 0);

	if (fallocate(fd, mode, offset, length) != 0)
		die_errno("fallocate failed");

	if (fstat(fd, &sb) != 0)
		die_errno("fstat failed");

	printf("fallocated a file: i_size=%"PRIu64", i_blocks=%"PRIu64"\n", sb.st_size, sb.st_blocks);
	exit(0);
}

#define erase_desc "erase a block device"
#define erase_help				\
"f2fs_io erase [block_device_path]\n\n"		\
"Send DISCARD | BLKSECDISCARD comamnd to"	\
"block device in block_device_path\n"		\

static void do_erase(int argc, char **argv, const struct cmd_desc *cmd)
{
	int fd, ret;
	struct stat st;
	u64 range[2];

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	if (stat(argv[1], &st) != 0) {
		fputs("stat error\n", stderr);
		exit(1);
	}

	if (!S_ISBLK(st.st_mode)) {
		fputs(argv[1], stderr);
		fputs(" is not a block device\n", stderr);
		exit(1);
	}

	fd = xopen(argv[1], O_WRONLY, 0);

	range[0] = 0;
	ret = ioctl(fd, BLKGETSIZE64, &range[1]);
	if (ret < 0) {
		fputs("get size failed\n", stderr);
		exit(1);
	}

	ret = ioctl(fd, BLKSECDISCARD, &range);
	if (ret < 0) {
		ret = ioctl(fd, BLKDISCARD, &range);
		if (ret < 0) {
			fputs("Discard failed\n", stderr);
			exit(1);
		}
	}

	exit(0);
}

static void do_write_with_advice(int argc, char **argv,
			const struct cmd_desc *cmd, bool with_advice)
{
	u64 buf_size = 0, inc_num = 0, written = 0;
	u64 offset;
	char *buf = NULL;
	unsigned bs, count, i;
	int flags = 0;
	int fd;
	u64 total_time = 0, max_time = 0, max_time_t = 0;
	bool atomic_commit = false, atomic_abort = false, replace = false;
	int useconds = 0;

	srand(time(0));

	bs = atoi(argv[1]);
	if (bs > 1024)
		die("Too big chunk size - limit: 4MB");

	buf_size = bs * F2FS_DEFAULT_BLKSIZE;

	offset = atoi(argv[2]) * buf_size;

	buf = aligned_xalloc(F2FS_DEFAULT_BLKSIZE, buf_size);
	count = atoi(argv[3]);

	if (!strcmp(argv[4], "zero"))
		memset(buf, 0, buf_size);
	else if (strcmp(argv[4], "inc_num") && strcmp(argv[4], "rand"))
		die("Wrong pattern type");

	if (!strcmp(argv[5], "dio")) {
		flags |= O_DIRECT;
	} else if (!strcmp(argv[5], "dsync")) {
		flags |= O_DIRECT | O_DSYNC;
	} else if (!strcmp(argv[5], "osync")) {
		flags |= O_SYNC;
	} else if (!strcmp(argv[5], "atomic_commit")) {
		atomic_commit = true;
	} else if (!strcmp(argv[5], "atomic_abort")) {
		atomic_abort = true;
	} else if (!strcmp(argv[5], "atomic_rcommit")) {
		atomic_commit = true;
		replace = true;
	} else if (!strcmp(argv[5], "atomic_rabort")) {
		atomic_abort = true;
		replace = true;
	} else if (strcmp(argv[5], "buffered")) {
		die("Wrong IO type");
	}

	if (!with_advice) {
		fd = xopen(argv[6], O_CREAT | O_WRONLY | flags, 0755);
	} else {
		unsigned char advice;
		int ret;

		if (!strcmp(argv[6], "hot"))
			advice = FADVISE_HOT_BIT;
		else if (!strcmp(argv[6], "cold"))
			advice = FADVISE_COLD_BIT;
		else
			die("Wrong Advise type");

		fd = xopen(argv[7], O_CREAT | O_WRONLY | flags, 0755);

		ret = fsetxattr(fd, F2FS_SYSTEM_ADVISE_NAME,
				    (char *)&advice, 1, XATTR_CREATE);
		if (ret) {
			fputs("fsetxattr advice failed\n", stderr);
			exit(1);
		}
	}

	total_time = get_current_us();
	if (atomic_commit || atomic_abort) {
		int ret;

		if (argc == 8)
			useconds = atoi(argv[7]) * 1000 / (count + 2);

		if (useconds)
			usleep(useconds);

		if (replace)
			ret = ioctl(fd, F2FS_IOC_START_ATOMIC_REPLACE);
		else
			ret = ioctl(fd, F2FS_IOC_START_ATOMIC_WRITE);

		if (useconds)
			usleep(useconds);

		if (ret < 0) {
			fputs("setting atomic file mode failed\n", stderr);
			exit(1);
		}
	}

	for (i = 0; i < count; i++) {
		uint64_t ret;

		if (!strcmp(argv[4], "inc_num"))
			*(int *)buf = inc_num++;
		else if (!strcmp(argv[4], "rand"))
			*(int *)buf = rand();

		/* write data */
		max_time_t = get_current_us();
		ret = pwrite(fd, buf, buf_size, offset + buf_size * i);
		max_time_t = get_current_us() - max_time_t;
		if (max_time < max_time_t)
			max_time = max_time_t;
		if (ret != buf_size)
			break;
		written += ret;

		if (useconds)
			usleep(useconds);
	}

	if (atomic_commit) {
		int ret;

		ret = ioctl(fd, F2FS_IOC_COMMIT_ATOMIC_WRITE);
		if (ret < 0) {
			fputs("committing atomic write failed\n", stderr);
			exit(1);
		}
	} else if (atomic_abort) {
		int ret;

		ret = ioctl(fd, F2FS_IOC_ABORT_VOLATILE_WRITE);
		if (ret < 0) {
			fputs("aborting atomic write failed\n", stderr);
			exit(1);
		}
	}

	printf("Written %"PRIu64" bytes with pattern=%s, total_time=%"PRIu64" us, max_latency=%"PRIu64" us\n",
				written, argv[4],
				get_current_us() - total_time,
				max_time);
	exit(0);
}

#define write_desc "write data into file"
#define write_help					\
"f2fs_io write [chunk_size in 4kb] [offset in chunk_size] [count] [pattern] [IO] [file_path] {delay}\n\n"	\
"Write given patten data in file_path\n"		\
"pattern can be\n"					\
"  zero          : zeros\n"				\
"  inc_num       : incrementing numbers\n"		\
"  rand          : random numbers\n"			\
"IO can be\n"						\
"  buffered      : buffered IO\n"			\
"  dio           : O_DIRECT\n"				\
"  dsync         : O_DIRECT | O_DSYNC\n"		\
"  osync         : O_SYNC\n"				\
"  atomic_commit : atomic write & commit\n"		\
"  atomic_abort  : atomic write & abort\n"		\
"  atomic_rcommit: atomic replace & commit\n"		\
"  atomic_rabort : atomic replace & abort\n"		\
"{delay} is in ms unit and optional only for atomic operations\n"

static void do_write(int argc, char **argv, const struct cmd_desc *cmd)
{
	if (argc < 7 || argc > 8) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}
	do_write_with_advice(argc, argv, cmd, false);
}

#define write_advice_desc "write data into file with a hint"
#define write_advice_help					\
"f2fs_io write_advice [chunk_size in 4kb] [offset in chunk_size] [count] [pattern] [IO] [advise] [file_path] {delay}\n\n"	\
"Write given patten data in file_path\n"		\
"pattern can be\n"					\
"  zero          : zeros\n"				\
"  inc_num       : incrementing numbers\n"		\
"  rand          : random numbers\n"			\
"IO can be\n"						\
"  buffered      : buffered IO\n"			\
"  dio           : O_DIRECT\n"				\
"  dsync         : O_DIRECT | O_DSYNC\n"		\
"  osync         : O_SYNC\n"				\
"  atomic_commit : atomic write & commit\n"		\
"  atomic_abort  : atomic write & abort\n"		\
"  atomic_rcommit: atomic replace & commit\n"		\
"  atomic_rabort : atomic replace & abort\n"		\
"advise can be\n"					\
"  cold : indicate a cold file\n"			\
"  hot  : indicate a hot file\n"			\
"{delay} is in ms unit and optional only for atomic operations\n"

static void do_write_advice(int argc, char **argv, const struct cmd_desc *cmd)
{
	if (argc < 8 || argc > 9) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}
	do_write_with_advice(argc, argv, cmd, true);
}

#define read_desc "read data from file"
#define read_help					\
"f2fs_io read [chunk_size in 4kb] [offset in chunk_size] [count] [IO] [advice] [print_nbytes] [file_path]\n\n"	\
"Read data in file_path and print nbytes\n"		\
"IO can be\n"						\
"  buffered : buffered IO\n"				\
"  dio      : direct IO\n"				\
"  mmap     : mmap IO\n"				\
"advice can be\n"					\
" 1 : set sequential|willneed\n"			\
" 0 : none\n"						\

static void do_read(int argc, char **argv, const struct cmd_desc *cmd)
{
	u64 buf_size = 0, ret = 0, read_cnt = 0;
	u64 offset;
	char *buf = NULL;
	char *data;
	char *print_buf = NULL;
	unsigned bs, count, i, print_bytes;
	u64 total_time = 0;
	int flags = 0;
	int do_mmap = 0;
	int fd, advice;

	if (argc != 8) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	bs = atoi(argv[1]);
	if (bs > 256 * 1024)
		die("Too big chunk size - limit: 1GB");
	buf_size = bs * F2FS_DEFAULT_BLKSIZE;

	offset = atoi(argv[2]) * buf_size;

	buf = aligned_xalloc(F2FS_DEFAULT_BLKSIZE, buf_size);

	count = atoi(argv[3]);
	if (!strcmp(argv[4], "dio"))
		flags |= O_DIRECT;
	else if (!strcmp(argv[4], "mmap"))
		do_mmap = 1;
	else if (strcmp(argv[4], "buffered"))
		die("Wrong IO type");

	print_bytes = atoi(argv[6]);
	if (print_bytes > buf_size)
		die("Print_nbytes should be less then chunk_size in kb");

	print_buf = xmalloc(print_bytes);

	fd = xopen(argv[7], O_RDONLY | flags, 0);

	advice = atoi(argv[5]);
	if (advice) {
		if (posix_fadvise(fd, 0, F2FS_DEFAULT_BLKSIZE,
				POSIX_FADV_SEQUENTIAL) != 0)
			die_errno("fadvise failed");
		if (posix_fadvise(fd, 0, F2FS_DEFAULT_BLKSIZE,
				POSIX_FADV_WILLNEED) != 0)
			die_errno("fadvise failed");
		printf("fadvise SEQUENTIAL|WILLNEED to a file: %s\n", argv[7]);
	}

	total_time = get_current_us();
	if (do_mmap) {
		data = mmap(NULL, count * buf_size, PROT_READ,
						MAP_SHARED | MAP_POPULATE, fd, offset);
		if (data == MAP_FAILED)
			die("Mmap failed");
	}
	if (!do_mmap) {
		for (i = 0; i < count; i++) {
			ret = pread(fd, buf, buf_size, offset + buf_size * i);
			if (ret != buf_size) {
				printf("pread expected: %"PRIu64", readed: %"PRIu64"\n",
						buf_size, ret);
				if (ret > 0) {
					read_cnt += ret;
					memcpy(print_buf, buf, print_bytes);
				}
				break;
			}

			read_cnt += ret;
			if (i == 0)
				memcpy(print_buf, buf, print_bytes);
		}
	} else {
		read_cnt = count * buf_size;
		memcpy(print_buf, data, print_bytes);
	}
	printf("Read %"PRIu64" bytes total_time = %"PRIu64" us, BW = %.Lf MB/s print %u bytes:\n",
		read_cnt, get_current_us() - total_time,
		((long double)read_cnt / (get_current_us() - total_time)), print_bytes);
	printf("%08"PRIx64" : ", offset);
	for (i = 1; i <= print_bytes; i++) {
		printf("%02x", print_buf[i - 1]);
		if (i % 16 == 0)
			printf("\n%08"PRIx64" : ", offset + 16 * i);
		else if (i % 2 == 0)
			printf(" ");
	}
	printf("\n");
	exit(0);
}

#define fragread_desc "read data with a fragmented buffer from file"
#define fragread_help					\
"f2fs_io fragread [chunk_size in 4kb] [offset in chunk_size] [count] [advice] [file_path]\n\n"	\
"Read data in file_path and print nbytes\n"		\
"advice can be\n"					\
" 1 : set sequential|willneed\n"			\
" 0 : none\n"						\

#ifndef PAGE_SIZE
#define PAGE_SIZE sysconf(_SC_PAGESIZE)
#endif
#define ALLOC_SIZE (2 * 1024 * 1024 - 4 * 1024) // 2MB - 4KB

static void do_fragread(int argc, char **argv, const struct cmd_desc *cmd)
{
	u64 buf_size = 0, ret = 0, read_cnt = 0;
	u64 offset;
	char *buf = NULL;
	uintptr_t idx, ptr;
	unsigned bs, count, i;
	u64 total_time = 0;
	int flags = 0, alloc_count = 0;
	void *mem_hole, **mem_holes;
	int fd, advice;

	if (argc != 6) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	bs = atoi(argv[1]);
	if (bs > 256 * 1024)
		die("Too big chunk size - limit: 1GB");
	buf_size = bs * F2FS_DEFAULT_BLKSIZE;

	offset = atoi(argv[2]) * buf_size;
	count = atoi(argv[3]);
	advice = atoi(argv[4]);
	mem_holes = xmalloc(sizeof(void *) * (buf_size / PAGE_SIZE));

	/* 1. Allocate the buffer using mmap. */
	buf = mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	/* 2. Loop and touch each page. */
	for (idx = (uintptr_t)buf; idx < (uintptr_t)buf + buf_size;
						idx += PAGE_SIZE)
	{
		/* Touch the current page. */
		volatile char *page = (volatile char *)idx;
		*page;

		/* 3. Allocate (2M - 4K) memory using mmap and touch all of it. */
		mem_hole = mmap(NULL, ALLOC_SIZE, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (mem_hole == MAP_FAILED)
			die_errno("map failed");

		/* Store the allocated memory pointer. */
		mem_holes[alloc_count++] = mem_hole;

		/* Touch all allocated memory. */
		for (ptr = (uintptr_t)mem_hole;
			ptr < (uintptr_t)mem_hole + ALLOC_SIZE;
						ptr += PAGE_SIZE) {
			volatile char *alloc_page = (volatile char *)ptr;
			*alloc_page;
		}
	}
	printf("Touched allocated memory: count = %u\n", alloc_count);
	printf(" - allocated memory: = ");
	for (idx = 0; idx < 5; idx++)
		printf(" %p", mem_holes[idx]);
	printf("\n");

	/* Pin the pages. */
	if (mlock(buf, buf_size))
		die_errno("mlock failed");

	fd = xopen(argv[5], O_RDONLY | flags, 0);

	if (advice) {
		if (posix_fadvise(fd, 0, F2FS_DEFAULT_BLKSIZE,
				POSIX_FADV_SEQUENTIAL) != 0)
			die_errno("fadvise failed");
		if (posix_fadvise(fd, 0, F2FS_DEFAULT_BLKSIZE,
				POSIX_FADV_WILLNEED) != 0)
			die_errno("fadvise failed");
		printf("fadvise SEQUENTIAL|WILLNEED to a file: %s\n", argv[5]);
	}

	total_time = get_current_us();

	for (i = 0; i < count; i++) {
		ret = pread(fd, buf, buf_size, offset + buf_size * i);
		if (ret != buf_size) {
			printf("pread expected: %"PRIu64", readed: %"PRIu64"\n",
					buf_size, ret);
			if (ret > 0)
				read_cnt += ret;
			break;
		}

		read_cnt += ret;
	}
	printf("Fragmented_Read %"PRIu64" bytes total_time = %"PRIu64" us, BW = %.Lf MB/s\n",
		read_cnt, get_current_us() - total_time,
		((long double)read_cnt / (get_current_us() - total_time)));
	printf("\n");
	exit(0);
}

#define randread_desc "random read data from file"
#define randread_help					\
"f2fs_io randread [chunk_size in 4kb] [count] [IO] [advise] [file_path]\n\n"	\
"Do random read data in file_path\n"		\
"IO can be\n"						\
"  buffered : buffered IO\n"				\
"  dio      : direct IO\n"				\
"  mmap     : mmap IO\n"				\
"advice can be\n"					\
" 1 : set random|willneed\n"				\
" 0 : none\n"						\

static void do_randread(int argc, char **argv, const struct cmd_desc *cmd)
{
	u64 buf_size = 0, ret = 0, read_cnt = 0;
	u64 idx, end_idx, aligned_size;
	char *buf = NULL;
	char *data;
	unsigned bs, count, i, j;
	u64 total_time = 0, elapsed_time = 0;
	int flags = 0;
	int do_mmap = 0;
	int fd, advice;
	time_t t;
	struct stat stbuf;

	if (argc != 6) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	bs = atoi(argv[1]);
	if (bs > 1024)
		die("Too big chunk size - limit: 4MB");
	buf_size = bs * F2FS_DEFAULT_BLKSIZE;

	buf = aligned_xalloc(F2FS_DEFAULT_BLKSIZE, buf_size);

	count = atoi(argv[2]);
	if (!strcmp(argv[3], "dio"))
		flags |= O_DIRECT;
	else if (!strcmp(argv[3], "mmap"))
		do_mmap = 1;
	else if (strcmp(argv[3], "buffered"))
		die("Wrong IO type");

	fd = xopen(argv[5], O_RDONLY | flags, 0);

	advice = atoi(argv[4]);
	if (advice) {
		if (posix_fadvise(fd, 0, stbuf.st_size, POSIX_FADV_RANDOM) != 0)
			die_errno("fadvise failed");
		if (posix_fadvise(fd, 0, 4096, POSIX_FADV_WILLNEED) != 0)
			die_errno("fadvise failed");
		printf("fadvise RANDOM|WILLNEED to a file: %s\n", argv[5]);
	}

	if (fstat(fd, &stbuf) != 0)
		die_errno("fstat of source file failed");

	aligned_size = (u64)stbuf.st_size & ~((u64)(F2FS_DEFAULT_BLKSIZE - 1));
	if (aligned_size < buf_size)
		die("File is too small to random read");
	end_idx = (u64)(aligned_size - buf_size) / (u64)F2FS_DEFAULT_BLKSIZE + 1;

	if (do_mmap) {
		data = mmap(NULL, stbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
		if (data == MAP_FAILED)
			die("Mmap failed");
		if (madvise((void *)data, stbuf.st_size, MADV_RANDOM) != 0)
			die_errno("madvise failed");
	}

	srand((unsigned) time(&t));

	total_time = get_current_us();

	for (i = 0; i < count; i++) {
		idx = rand() % end_idx;

		if (!do_mmap) {
			ret = pread(fd, buf, buf_size, 4096 * idx);
			if (ret != buf_size)
				break;
		} else {
			for (j = 0; j < bs; j++)
				*buf = data[4096 * (idx + j)];
		}
		read_cnt += buf_size;
	}
	elapsed_time = get_current_us() - total_time;

	printf("Read %"PRIu64" bytes total_time = %"PRIu64" us, avg. latency = %.Lf us, IOPs= %.Lf, BW = %.Lf MB/s\n",
		read_cnt, elapsed_time,
		(long double)elapsed_time / count,
		(long double)count * 1000 * 1000 / elapsed_time,
		(long double)read_cnt / elapsed_time);
	exit(0);
}

#define fiemap_desc "get block address in file"
#define fiemap_help					\
"f2fs_io fiemap [offset in 4kb] [count in 4kb] [file_path]\n\n"\

#if defined(HAVE_LINUX_FIEMAP_H) && defined(HAVE_LINUX_FS_H)
static void do_fiemap(int argc, char **argv, const struct cmd_desc *cmd)
{
	unsigned int i;
	int fd, extents_mem_size;
	u64 start, length;
	u32 mapped_extents;
	struct fiemap *fm = xmalloc(sizeof(struct fiemap));

	if (argc != 4) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	memset(fm, 0, sizeof(struct fiemap));
	start = (u64)atoi(argv[1]) * F2FS_DEFAULT_BLKSIZE;
	length = (u64)atoi(argv[2]) * F2FS_DEFAULT_BLKSIZE;
	fm->fm_start = start;
	fm->fm_length = length;

	fd = xopen(argv[3], O_RDONLY | O_LARGEFILE, 0);

	printf("Fiemap: offset = %"PRIu64" len = %"PRIu64"\n",
				start / F2FS_DEFAULT_BLKSIZE,
				length / F2FS_DEFAULT_BLKSIZE);
	if (ioctl(fd, FS_IOC_FIEMAP, fm) < 0)
		die_errno("FIEMAP failed");

	mapped_extents = fm->fm_mapped_extents;
	extents_mem_size = sizeof(struct fiemap_extent) * mapped_extents;
	free(fm);
	fm = xmalloc(sizeof(struct fiemap) + extents_mem_size);

	memset(fm, 0, sizeof(struct fiemap) + extents_mem_size);
	fm->fm_start = start;
	fm->fm_length = length;
	fm->fm_extent_count = mapped_extents;

	if (ioctl(fd, FS_IOC_FIEMAP, fm) < 0)
		die_errno("FIEMAP failed");

	printf("\t%-17s%-17s%-17s%s\n", "logical addr.", "physical addr.", "length", "flags");
	for (i = 0; i < fm->fm_mapped_extents; i++) {
		printf("%d\t%.16llx %.16llx %.16llx %.8x\n", i,
		    fm->fm_extents[i].fe_logical, fm->fm_extents[i].fe_physical,
		    fm->fm_extents[i].fe_length, fm->fm_extents[i].fe_flags);

		if (fm->fm_extents[i].fe_flags & FIEMAP_EXTENT_LAST)
			break;
	}
	printf("\n");
	free(fm);
	exit(0);
}
#else
static void do_fiemap(int UNUSED(argc), char **UNUSED(argv),
			const struct cmd_desc *UNUSED(cmd))
{
	die("Not support for this platform");
}
#endif

#define gc_urgent_desc "start/end/run gc_urgent for given time period"
#define gc_urgent_help					\
"f2fs_io gc_urgent $dev [start/end/run] [time in sec]\n\n"\
" - f2fs_io gc_urgent sda21 start\n"		\
" - f2fs_io gc_urgent sda21 end\n"		\
" - f2fs_io gc_urgent sda21 run 10\n"		\

static void do_gc_urgent(int argc, char **argv, const struct cmd_desc *cmd)
{
	char command[255];

	if (argc == 3 && !strcmp(argv[2], "start")) {
		printf("gc_urgent: start on %s\n", argv[1]);
		sprintf(command, "echo %d > %s/%s/gc_urgent", 1, "/sys/fs/f2fs/", argv[1]);
		if (system(command))
			exit(1);
	} else if (argc == 3 && !strcmp(argv[2], "end")) {
		printf("gc_urgent: end on %s\n", argv[1]);
		sprintf(command, "echo %d > %s/%s/gc_urgent", 0, "/sys/fs/f2fs/", argv[1]);
		if (system(command))
			exit(1);
	} else if (argc == 4 && !strcmp(argv[2], "run")) {
		printf("gc_urgent: start on %s for %d secs\n", argv[1], atoi(argv[3]));
		sprintf(command, "echo %d > %s/%s/gc_urgent", 1, "/sys/fs/f2fs/", argv[1]);
		if (system(command))
			exit(1);
		sleep(atoi(argv[3]));
		printf("gc_urgent: end on %s for %d secs\n", argv[1], atoi(argv[3]));
		sprintf(command, "echo %d > %s/%s/gc_urgent", 0, "/sys/fs/f2fs/", argv[1]);
		if (system(command))
			exit(1);
	} else {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}
}

#define defrag_file_desc "do defragment on file"
#define defrag_file_help						\
"f2fs_io defrag_file [start] [length] [file_path]\n\n"		\
"  start     : start offset of defragment region, unit: bytes\n"	\
"  length    : bytes number of defragment region\n"			\

static void do_defrag_file(int argc, char **argv, const struct cmd_desc *cmd)
{
	struct f2fs_defragment df;
	u64 len;
	int ret, fd;

	if (argc != 4) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	df.start = atoll(argv[1]);
	df.len = len = atoll(argv[2]);

	fd = xopen(argv[3], O_RDWR, 0);

	ret = ioctl(fd, F2FS_IOC_DEFRAGMENT, &df);
	if (ret < 0)
		die_errno("F2FS_IOC_DEFRAGMENT failed");

	printf("defrag %s in region[%"PRIu64", %"PRIu64"]\n",
			argv[3], df.start, df.start + len);
	exit(0);
}

#define copy_desc "copy a file"
#define copy_help							\
"f2fs_io copy [-d] [-m] [-s] src_path dst_path\n\n"			\
"  src_path  : path to source file\n"					\
"  dst_path  : path to destination file\n"				\
"  -d        : use direct I/O\n"					\
"  -m        : mmap the source file\n"					\
"  -s        : use sendfile\n"						\

static void do_copy(int argc, char **argv, const struct cmd_desc *cmd)
{
	int c;
	int src_fd;
	int dst_fd;
	int open_flags = 0;
	bool mmap_source_file = false;
	bool use_sendfile = false;
	ssize_t ret;

	while ((c = getopt(argc, argv, "dms")) != -1) {
		switch (c) {
		case 'd':
			open_flags |= O_DIRECT;
			break;
		case 'm':
			mmap_source_file = true;
			break;
		case 's':
			use_sendfile = true;
			break;
		default:
			fputs(cmd->cmd_help, stderr);
			exit(2);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 2) {
		fputs("Wrong number of arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(2);
	}
	if (mmap_source_file && use_sendfile)
		die("-m and -s are mutually exclusive");

	src_fd = xopen(argv[0], O_RDONLY | open_flags, 0);
	dst_fd = xopen(argv[1], O_WRONLY | O_CREAT | O_TRUNC | open_flags, 0644);

	if (mmap_source_file) {
		struct stat stbuf;
		void *src_addr;

		if (fstat(src_fd, &stbuf) != 0)
			die_errno("fstat of source file failed");

		if ((size_t)stbuf.st_size != stbuf.st_size)
			die("Source file is too large");

		src_addr = mmap(NULL, stbuf.st_size, PROT_READ, MAP_SHARED,
				src_fd, 0);
		if (src_addr == MAP_FAILED)
			die("mmap of source file failed");

		full_write(dst_fd, src_addr, stbuf.st_size);

		munmap(src_addr, stbuf.st_size);
	} else if (use_sendfile) {
		while ((ret = sendfile(dst_fd, src_fd, NULL, INT_MAX)) > 0)
			;
		if (ret < 0)
			die_errno("sendfile failed");
	} else {
		char *buf = aligned_xalloc(F2FS_DEFAULT_BLKSIZE, F2FS_DEFAULT_BLKSIZE);

		while ((ret = xread(src_fd, buf, F2FS_DEFAULT_BLKSIZE)) > 0)
			full_write(dst_fd, buf, ret);
		free(buf);
	}
	close(src_fd);
	close(dst_fd);
}

#define get_cblocks_desc "get number of reserved blocks on compress inode"
#define get_cblocks_help "f2fs_io get_cblocks [file]\n\n"

static void do_get_cblocks(int argc, char **argv, const struct cmd_desc *cmd)
{
	unsigned long long blkcnt;
	int ret, fd;

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[1], O_RDONLY, 0);

	ret = ioctl(fd, F2FS_IOC_GET_COMPRESS_BLOCKS, &blkcnt);
	if (ret < 0)
		die_errno("F2FS_IOC_GET_COMPRESS_BLOCKS failed");

	printf("%llu\n", blkcnt);

	exit(0);
}

#define release_cblocks_desc "release reserved blocks on compress inode"
#define release_cblocks_help "f2fs_io release_cblocks [file]\n\n"

static void do_release_cblocks(int argc, char **argv, const struct cmd_desc *cmd)
{
	unsigned long long blkcnt;
	int ret, fd;

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[1], O_RDONLY, 0);

	ret = ioctl(fd, F2FS_IOC_RELEASE_COMPRESS_BLOCKS, &blkcnt);
	if (ret < 0)
		die_errno("F2FS_IOC_RELEASE_COMPRESS_BLOCKS failed");

	printf("%llu\n", blkcnt);

	exit(0);
}

#define reserve_cblocks_desc "reserve blocks on compress inode"
#define reserve_cblocks_help "f2fs_io reserve_cblocks [file]\n\n"

static void do_reserve_cblocks(int argc, char **argv, const struct cmd_desc *cmd)
{
	unsigned long long blkcnt;
	int ret, fd;

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[1], O_RDONLY, 0);

	ret = ioctl(fd, F2FS_IOC_RESERVE_COMPRESS_BLOCKS, &blkcnt);
	if (ret < 0)
		die_errno("F2FS_IOC_RESERVE_COMPRESS_BLOCKS failed");

	printf("%llu\n", blkcnt);

	exit(0);
}

#define get_coption_desc "get compression option of a compressed file"
#define get_coption_help						\
"f2fs_io get_coption [file]\n\n"	\
"  algorithm        : compression algorithm (0:lzo, 1: lz4, 2:zstd, 3:lzorle)\n"	\
"  log_cluster_size : compression cluster log size (2 <= log_size <= 8)\n"

static void do_get_coption(int argc, char **argv, const struct cmd_desc *cmd)
{
	struct f2fs_comp_option option;
	int ret, fd;

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[1], O_RDONLY, 0);

	ret = ioctl(fd, F2FS_IOC_GET_COMPRESS_OPTION, &option);
	if (ret < 0)
		die_errno("F2FS_IOC_GET_COMPRESS_OPTION failed");

	printf("compression algorithm:%u\n", option.algorithm);
	printf("compression cluster log size:%u\n", option.log_cluster_size);

	exit(0);
}

#define set_coption_desc "set compression option of a compressed file"
#define set_coption_help						\
"f2fs_io set_coption [algorithm] [log_cluster_size] [file_path]\n\n"	\
"  algorithm        : compression algorithm (0:lzo, 1: lz4, 2:zstd, 3:lzorle)\n"	\
"  log_cluster_size : compression cluster log size (2 <= log_size <= 8)\n"

static void do_set_coption(int argc, char **argv, const struct cmd_desc *cmd)
{
	struct f2fs_comp_option option;
	int fd, ret;

	if (argc != 4) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	option.algorithm = atoi(argv[1]);
	option.log_cluster_size = atoi(argv[2]);

	fd = xopen(argv[3], O_WRONLY, 0);

	ret = ioctl(fd, F2FS_IOC_SET_COMPRESS_OPTION, &option);
	if (ret < 0)
		die_errno("F2FS_IOC_SET_COMPRESS_OPTION failed");

	printf("set compression option: algorithm=%u, log_cluster_size=%u\n",
			option.algorithm, option.log_cluster_size);
	exit(0);
}

#define decompress_desc "decompress an already compressed file"
#define decompress_help "f2fs_io decompress [file_path]\n\n"

static void do_decompress(int argc, char **argv, const struct cmd_desc *cmd)
{
	int fd, ret;

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[1], O_WRONLY, 0);

	ret = ioctl(fd, F2FS_IOC_DECOMPRESS_FILE);
	if (ret < 0)
		die_errno("F2FS_IOC_DECOMPRESS_FILE failed");

	exit(0);
}

#define compress_desc "compress a compression enabled file"
#define compress_help "f2fs_io compress [file_path]\n\n"

static void do_compress(int argc, char **argv, const struct cmd_desc *cmd)
{
	int fd, ret;

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[1], O_WRONLY, 0);

	ret = ioctl(fd, F2FS_IOC_COMPRESS_FILE);
	if (ret < 0)
		die_errno("F2FS_IOC_COMPRESS_FILE failed");

	exit(0);
}

#define get_filename_encrypt_mode_desc "get file name encrypt mode"
#define get_filename_encrypt_mode_help					\
"f2fs_io filename_encrypt_mode [file or directory path]\n\n"		\
"Get the file name encription mode of the given file/directory.\n"	\

static void do_get_filename_encrypt_mode (int argc, char **argv,
						const struct cmd_desc *cmd)
{
	static const char *enc_name[] = {
		"invalid", /* FSCRYPT_MODE_INVALID (0) */
		"aes-256-xts", /* FSCRYPT_MODE_AES_256_XTS (1) */
		"aes-256-gcm", /* FSCRYPT_MODE_AES_256_GCM (2) */
		"aes-256-cbc", /* FSCRYPT_MODE_AES_256_CBC (3) */
		"aes-256-cts", /* FSCRYPT_MODE_AES_256_CTS (4) */
		"aes-128-cbc", /* FSCRYPT_MODE_AES_128_CBC (5) */
		"aes-128-cts", /* FSCRYPT_MODE_AES_128_CTS (6) */
		"speck128-256-xts", /* FSCRYPT_MODE_SPECK128_256_XTS (7) */
		"speck128-256-cts", /* FSCRYPT_MODE_SPECK128_256_CTS (8) */
		"adiantum", /* FSCRYPT_MODE_ADIANTUM (9) */
		"aes-256-hctr2", /* FSCRYPT_MODE_AES_256_HCTR2 (10) */
	};
	int fd, mode, ret;
	struct fscrypt_get_policy_ex_arg arg;

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[1], O_RDONLY, 0);
	arg.policy_size = sizeof(arg.policy);
	ret = ioctl(fd, FS_IOC_GET_ENCRYPTION_POLICY_EX, &arg);
	if (ret != 0 && errno == ENOTTY)
		ret = ioctl(fd, FS_IOC_GET_ENCRYPTION_POLICY, arg.policy.v1);
	close(fd);

	if (ret) {
		perror("FS_IOC_GET_ENCRYPTION_POLICY|_EX");
		exit(1);
	}

	switch (arg.policy.version) {
	case FSCRYPT_POLICY_V1:
		mode = arg.policy.v1.filenames_encryption_mode;
		break;
	case FSCRYPT_POLICY_V2:
		mode = arg.policy.v2.filenames_encryption_mode;
		break;
	default:
		printf("Do not support policy version: %d\n",
							arg.policy.version);
		exit(1);
	}

	if (mode >= sizeof(enc_name)/sizeof(enc_name[0])) {
		printf("Do not support algorithm: %d\n", mode);
		exit(1);
	}
	printf ("%s\n", enc_name[mode]);
	exit(0);
}

#define rename_desc "rename source to target file with fsync option"
#define rename_help							\
"f2fs_io rename [src_path] [target_path] [fsync_after_rename]\n\n"	\
"e.g., f2fs_io rename source dest 1\n"					\
"      1. open(source)\n"						\
"      2. rename(source, dest)\n"					\
"      3. fsync(source)\n"						\
"      4. close(source)\n"

static void do_rename(int argc, char **argv, const struct cmd_desc *cmd)
{
	int fd = -1;
	int ret;

	if (argc != 4) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	if (atoi(argv[3]))
		fd = xopen(argv[1], O_WRONLY, 0);

	ret = rename(argv[1], argv[2]);
	if (ret < 0)
		die_errno("rename failed");

	if (fd >= 0) {
		if (fsync(fd) != 0)
			die_errno("fsync failed: %s", argv[1]);
		close(fd);
	}
	exit(0);
}

#define gc_desc "trigger filesystem GC"
#define gc_help "f2fs_io gc sync_mode [file_path]\n\n"

static void do_gc(int argc, char **argv, const struct cmd_desc *cmd)
{
	u32 sync;
	int ret, fd;

	if (argc != 3) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	sync = atoi(argv[1]);

	fd = xopen(argv[2], O_RDONLY, 0);

	ret = ioctl(fd, F2FS_IOC_GARBAGE_COLLECT, &sync);
	if (ret < 0)
		die_errno("F2FS_IOC_GARBAGE_COLLECT failed");

	printf("trigger %s gc ret=%d\n",
		sync ? "synchronous" : "asynchronous", ret);
	exit(0);
}

#define checkpoint_desc "trigger filesystem checkpoint"
#define checkpoint_help "f2fs_io checkpoint [file_path]\n\n"

static void do_checkpoint(int argc, char **argv, const struct cmd_desc *cmd)
{
	int ret, fd;

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[1], O_WRONLY, 0);

	ret = ioctl(fd, F2FS_IOC_WRITE_CHECKPOINT);
	if (ret < 0)
		die_errno("F2FS_IOC_WRITE_CHECKPOINT failed");

	printf("trigger filesystem checkpoint ret=%d\n", ret);
	exit(0);
}

#define precache_extents_desc "trigger precache extents"
#define precache_extents_help "f2fs_io precache_extents [file_path]\n\n"

static void do_precache_extents(int argc, char **argv, const struct cmd_desc *cmd)
{
	int ret, fd;

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[1], O_WRONLY, 0);

	ret = ioctl(fd, F2FS_IOC_PRECACHE_EXTENTS);
	if (ret < 0)
		die_errno("F2FS_IOC_PRECACHE_EXTENTS failed");

	printf("trigger precache extents ret=%d\n", ret);
	exit(0);
}

#define move_range_desc "moving a range of data blocks from source file to destination file"
#define move_range_help						\
"f2fs_io move_range [src_path] [dst_path] [src_start] [dst_start] "	\
"[length]\n\n"								\
"  src_path  : path to source file\n"					\
"  dst_path  : path to destination file\n"				\
"  src_start : start offset of src file move region, unit: bytes\n"	\
"  dst_start : start offset of dst file move region, unit: bytes\n"	\
"  length    : size to move\n"						\

static void do_move_range(int argc, char **argv, const struct cmd_desc *cmd)
{
	struct f2fs_move_range range;
	int ret, fd;

	if (argc != 6) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	fd = xopen(argv[1], O_RDWR, 0);
	range.dst_fd = xopen(argv[2], O_RDWR | O_CREAT, 0644);
	range.pos_in = atoll(argv[3]);
	range.pos_out = atoll(argv[4]);
	range.len = atoll(argv[5]);

	ret = ioctl(fd, F2FS_IOC_MOVE_RANGE, &range);
	if (ret < 0)
		die_errno("F2FS_IOC_MOVE_RANGE failed");

	printf("move range ret=%d\n", ret);
	exit(0);
}

#define gc_range_desc "trigger filesystem gc_range"
#define gc_range_help "f2fs_io gc_range [sync_mode] [start] [length] [file_path]\n\n"\
"  sync_mode : 0: asynchronous, 1: synchronous\n"			\
"  start     : start offset of defragment region, unit: 4kb\n"	\
"  length    : bytes number of defragment region, unit: 4kb\n"	\

static void do_gc_range(int argc, char **argv, const struct cmd_desc *cmd)
{
	struct f2fs_gc_range range;
	int ret, fd;

	if (argc != 5) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	range.sync = atoi(argv[1]);
	range.start = (u64)atoi(argv[2]);
	range.len = (u64)atoi(argv[3]);

	fd = xopen(argv[4], O_RDWR, 0);

	ret = ioctl(fd, F2FS_IOC_GARBAGE_COLLECT_RANGE, &range);
	if (ret < 0) {
		die_errno("F2FS_IOC_GARBAGE_COLLECT_RANGE failed");
	}

	printf("trigger %s gc_range [%"PRIu64", %"PRIu64"] ret=%d\n",
		range.sync ? "synchronous" : "asynchronous",
		range.start, range.len, ret);
	exit(0);
}

#define listxattr_desc "listxattr"
#define listxattr_help "f2fs_io listxattr [file_path]\n\n"

static void do_listxattr(int argc, char **argv, const struct cmd_desc *cmd)
{
	char *buf, *key, *val;
	ssize_t buflen, vallen, keylen;

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	buflen = listxattr(argv[1], NULL, 0);
	if (buflen == -1) {
		perror("listxattr");
		exit(1);
	}
	if (buflen == 0) {
		printf("%s has no attributes.\n", argv[1]);
		exit(0);
	}
	buf = xmalloc(buflen);
	buflen = listxattr(argv[1], buf, buflen);
	if (buflen == -1) {
		perror("listxattr");
		exit(1);
	}

	key = buf;
	while (buflen > 0) {
		printf("%s: ", key);
		vallen = getxattr(argv[1], key, NULL, 0);
		if (vallen == -1) {
			perror("getxattr");
			exit(1);
		}
		if (vallen == 0) {
			printf("<no value>");
		} else {
			val = xmalloc(vallen + 1);
			vallen = getxattr(argv[1], key, val, vallen);
			if (vallen == -1) {
				perror("getxattr");
				exit(1);
			}
			val[vallen] = 0;
			printf("%s", val);
			free(val);
		}
		printf("\n");
		keylen = strlen(key) + 1;
		buflen -= keylen;
		key += keylen;
	}
	exit(0);
}

#define setxattr_desc "setxattr"
#define setxattr_help "f2fs_io setxattr [name] [value] [file_path]\n\n"

static void do_setxattr(int argc, char **argv, const struct cmd_desc *cmd)
{
	int ret;
	char *value;
	unsigned char tmp;

	if (argc != 4) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	if (!strcmp(argv[1], F2FS_SYSTEM_ADVISE_NAME)) {
		tmp = strtoul(argv[2], NULL, 0);
		value = (char *)&tmp;
	} else {
		value = argv[2];
	}

	ret = setxattr(argv[3], argv[1], value, strlen(argv[2]), XATTR_CREATE);
	printf("setxattr %s CREATE: name: %s, value: %s: ret=%d\n",
			argv[3], argv[1], argv[2], ret);
	if (ret < 0 && errno == EEXIST) {
		ret = setxattr(argv[3], argv[1], value, strlen(argv[2]), XATTR_REPLACE);
		printf("setxattr %s REPLACE: name: %s, value: %s: ret=%d\n",
				argv[3], argv[1], argv[2], ret);
	}
	if (ret < 0)
		perror("setxattr");
	exit(0);
}

#define removexattr_desc "removexattr"
#define removexattr_help "f2fs_io removexattr [name] [file_path]\n\n"

static void do_removexattr(int argc, char **argv, const struct cmd_desc *cmd)
{
	int ret;

	if (argc != 3) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	ret = removexattr(argv[2], argv[1]);
	printf("removexattr %s REMOVE: name: %s: ret=%d\n", argv[1], argv[2], ret);
	exit(0);
}

#define lseek_desc "do lseek for a file"
#define lseek_help					\
"f2fs_io lseek [whence] [offset] [file_path]\n\n"	\
"Do lseek file data in file_path and return the adjusted file offset\n"	\
"whence can be\n"					\
"  set  : SEEK_SET, The file offset is set to offset bytes\n"	\
"  cur  : SEEK_CUR, The file offset is set to its current location plus offset bytes\n"	\
"  end  : SEEK_END, The file offset is set to the size of the file plus offset bytes\n"	\
"  data : SEEK_DATA, set the file offset to the next data location from offset\n"	\
"  hole : SEEK_HOLE, set the file offset to the next hole from offset\n"

static void do_lseek(int argc, char **argv, const struct cmd_desc *cmd)
{
	int fd, whence;
	off_t offset, ret;

	if (argc != 4) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	offset = atoll(argv[2]);

	if (!strcmp(argv[1], "set"))
		whence = SEEK_SET;
	else if (!strcmp(argv[1], "cur"))
		whence = SEEK_CUR;
	else if (!strcmp(argv[1], "end"))
		whence = SEEK_END;
	else if (!strcmp(argv[1], "data"))
		whence = SEEK_DATA;
	else if (!strcmp(argv[1], "hole"))
		whence = SEEK_HOLE;
	else
		die("Wrong whence type");

	fd = xopen(argv[3], O_RDONLY, 0);

	ret = lseek(fd, offset, whence);
	if (ret < 0)
		die_errno("lseek failed");
	printf("returned offset=%lld\n", (long long)ret);
	exit(0);
}

#define get_advise_desc "get_advise"
#define get_advise_help "f2fs_io get_advise [file_path]\n\n"

static void do_get_advise(int argc, char **argv, const struct cmd_desc *cmd)
{
	int ret;
	unsigned char value;

	if (argc != 2) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	ret = getxattr(argv[1], F2FS_SYSTEM_ADVISE_NAME, &value, sizeof(value));
	if (ret != sizeof(value)) {
		perror("getxattr");
		exit(1);
	}

	printf("i_advise=0x%x, advise_type: ", value);
	if (value & FADVISE_COLD_BIT)
		printf("cold ");
	if (value & FADVISE_LOST_PINO_BIT)
		printf("lost_pino ");
	if (value & FADVISE_ENCRYPT_BIT)
		printf("encrypt ");
	if (value & FADVISE_ENC_NAME_BIT)
		printf("enc_name ");
	if (value & FADVISE_KEEP_SIZE_BIT)
		printf("keep_size ");
	if (value & FADVISE_HOT_BIT)
		printf("hot ");
	if (value & FADVISE_VERITY_BIT)
		printf("verity ");
	if (value & FADVISE_TRUNC_BIT)
		printf("trunc ");
	printf("\n");
}

#define ftruncate_desc "ftruncate a file"
#define ftruncate_help					\
"f2fs_io ftruncate [length] [file_path]\n\n"	\
"Do ftruncate a file in file_path with the length\n"	\

static void do_ftruncate(int argc, char **argv, const struct cmd_desc *cmd)
{
	int fd, ret;
	off_t length;

	if (argc != 3) {
		fputs("Excess arguments\n\n", stderr);
		fputs(cmd->cmd_help, stderr);
		exit(1);
	}

	length = atoll(argv[1]);
	fd = xopen(argv[2], O_WRONLY, 0);

	ret = ftruncate(fd, length);
	if (ret < 0)
		die_errno("ftruncate failed");
	exit(0);
}

#define CMD_HIDDEN 	0x0001
#define CMD(name) { #name, do_##name, name##_desc, name##_help, 0 }
#define _CMD(name) { #name, do_##name, NULL, NULL, CMD_HIDDEN }

static void do_help(int argc, char **argv, const struct cmd_desc *cmd);
const struct cmd_desc cmd_list[] = {
	_CMD(help),
	CMD(fsync),
	CMD(fdatasync),
	CMD(set_verity),
	CMD(getflags),
	CMD(setflags),
	CMD(clearflags),
	CMD(shutdown),
	CMD(pinfile),
	CMD(fadvise),
	CMD(fallocate),
	CMD(erase),
	CMD(write),
	CMD(write_advice),
	CMD(read),
	CMD(randread),
	CMD(fragread),
	CMD(fiemap),
	CMD(gc_urgent),
	CMD(defrag_file),
	CMD(copy),
	CMD(get_cblocks),
	CMD(release_cblocks),
	CMD(reserve_cblocks),
	CMD(get_coption),
	CMD(set_coption),
	CMD(decompress),
	CMD(compress),
	CMD(get_filename_encrypt_mode),
	CMD(rename),
	CMD(gc),
	CMD(checkpoint),
	CMD(precache_extents),
	CMD(move_range),
	CMD(gc_range),
	CMD(listxattr),
	CMD(setxattr),
	CMD(removexattr),
	CMD(lseek),
	CMD(get_advise),
	CMD(ioprio),
	CMD(ftruncate),
	{ NULL, NULL, NULL, NULL, 0 }
};

static void do_help(int argc, char **argv, const struct cmd_desc *UNUSED(cmd))
{
	const struct cmd_desc *p;

	if (argc > 1) {
		for (p = cmd_list; p->cmd_name; p++) {
			if (p->cmd_flags & CMD_HIDDEN)
				continue;
			if (strcmp(p->cmd_name, argv[1]) == 0) {
				putc('\n', stdout);
				fputs("USAGE:\n  ", stdout);
				fputs(p->cmd_help, stdout);
				exit(0);
			}
		}
		printf("Unknown command: %s\n\n", argv[1]);
	}

	fputs("Available commands:\n", stdout);
	for (p = cmd_list; p->cmd_name; p++) {
		if (p->cmd_flags & CMD_HIDDEN)
			continue;
		printf("  %-20s %s\n", p->cmd_name, p->cmd_desc);
	}
	printf("\nTo get more information on a command, "
	       "type 'f2fs_io help cmd'\n");
	exit(0);
}

static void die_signal_handler(int UNUSED(signum), siginfo_t *UNUSED(siginfo),
				void *UNUSED(context))
{
	exit(-1);
}

static void sigcatcher_setup(void)
{
	struct sigaction	sa;

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_sigaction = die_signal_handler;
	sa.sa_flags = SA_SIGINFO;

	sigaction(SIGHUP, &sa, 0);
	sigaction(SIGINT, &sa, 0);
	sigaction(SIGQUIT, &sa, 0);
	sigaction(SIGFPE, &sa, 0);
	sigaction(SIGILL, &sa, 0);
	sigaction(SIGBUS, &sa, 0);
	sigaction(SIGSEGV, &sa, 0);
	sigaction(SIGABRT, &sa, 0);
	sigaction(SIGPIPE, &sa, 0);
	sigaction(SIGALRM, &sa, 0);
	sigaction(SIGTERM, &sa, 0);
	sigaction(SIGUSR1, &sa, 0);
	sigaction(SIGUSR2, &sa, 0);
	sigaction(SIGPOLL, &sa, 0);
	sigaction(SIGPROF, &sa, 0);
	sigaction(SIGSYS, &sa, 0);
	sigaction(SIGTRAP, &sa, 0);
	sigaction(SIGVTALRM, &sa, 0);
	sigaction(SIGXCPU, &sa, 0);
	sigaction(SIGXFSZ, &sa, 0);
}

int main(int argc, char **argv)
{
	const struct cmd_desc *cmd;

	if (argc < 2)
		do_help(argc, argv, cmd_list);

	sigcatcher_setup();
	for (cmd = cmd_list; cmd->cmd_name; cmd++) {
		if (strcmp(cmd->cmd_name, argv[1]) == 0) {
			cmd->cmd_func(argc - 1, argv + 1, cmd);
			exit(0);
		}
	}
	printf("Unknown command: %s\n\n", argv[1]);
	do_help(1, argv, cmd_list);
	return 0;
}
