/**
 * f2fs_fs.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 * Copyright (c) 2019 Google Inc.
 *             http://www.google.com/
 * Copyright (c) 2020 Google Inc.
 *   Robin Hsu <robinhsu@google.com>
 *  : add sload compression support
 *
 * Dual licensed under the GPL or LGPL version 2 licenses.
 *
 * The byteswap codes are copied from:
 *   samba_3_master/lib/ccan/endian/endian.h under LGPL 2.1
 */
#ifndef __F2FS_FS_H__
#define __F2FS_FS_H__

#ifndef __SANE_USERSPACE_TYPES__
#define __SANE_USERSPACE_TYPES__       /* For PPC64, to get LL64 types */
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <limits.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#else
#ifdef __ANDROID__
#define WITH_ANDROID
#endif
#endif /* HAVE_CONFIG_H */

#ifdef WITH_ANDROID
#include <android_config.h>
#else
#define WITH_DUMP
#define WITH_DEFRAG
#define WITH_RESIZE
#define WITH_SLOAD
#define WITH_LABEL
#define WITH_INJECT
#endif

#include <inttypes.h>
#ifdef HAVE_LINUX_TYPES_H
#include <linux/types.h>
#endif
#include <sys/types.h>

#ifdef HAVE_KERNEL_UAPI_LINUX_BLKZONED_H
#include <kernel/uapi/linux/blkzoned.h>
#elif defined(HAVE_LINUX_BLKZONED_H)
#include <linux/blkzoned.h>
#endif

#ifdef HAVE_LINUX_RW_HINT_H
#include <linux/rw_hint.h>
#else
enum rw_hint {
	WRITE_LIFE_NOT_SET	= 0,
	WRITE_LIFE_NONE,
	WRITE_LIFE_SHORT,
	WRITE_LIFE_MEDIUM,
	WRITE_LIFE_LONG,
	WRITE_LIFE_EXTREME
};
#endif

#ifdef HAVE_LIBSELINUX
#include <selinux/selinux.h>
#include <selinux/label.h>
#endif

#ifdef UNUSED
#elif defined(__GNUC__)
# define UNUSED(x) UNUSED_ ## x __attribute__((unused))
#elif defined(__LCLINT__)
# define UNUSED(x) x
#elif defined(__cplusplus)
# define UNUSED(x)
#else
# define UNUSED(x) x
#endif

#ifndef static_assert
#define static_assert _Static_assert
#endif

#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif

#ifndef fallthrough
#ifdef __clang__
#define fallthrough do {} while (0) /* fall through */
#else
#define fallthrough __attribute__((__fallthrough__))
#endif
#endif

#ifdef _WIN32
#undef HAVE_LINUX_TYPES_H
#endif

/* codes from kernel's f2fs.h, GPL-v2.0 */
#define MIN_COMPRESS_LOG_SIZE	2
#define MAX_COMPRESS_LOG_SIZE	8

typedef uint64_t	u64;
typedef uint32_t	u32;
typedef uint16_t	u16;
typedef uint8_t		u8;
typedef u32		block_t;
typedef u32		nid_t;
typedef unsigned long	pgoff_t;
typedef unsigned short	umode_t;

#ifndef HAVE_LINUX_TYPES_H
typedef u8	__u8;
typedef u16	__u16;
typedef u32	__u32;
typedef u64	__u64;
typedef u16	__le16;
typedef u32	__le32;
typedef u64	__le64;
typedef u16	__be16;
typedef u32	__be32;
typedef u64	__be64;
#endif

/*
 * code borrowed from kernel f2fs dirver: f2fs.h, GPL-2.0
 *  : definitions of COMPRESS_DATA_RESERVED_SIZE,
 *    struct compress_data, COMPRESS_HEADER_SIZE,
 *    and struct compress_ctx
 */
#define COMPRESS_DATA_RESERVED_SIZE		4
struct compress_data {
	__le32 clen;			/* compressed data size */
	__le32 chksum;			/* checksum of compressed data */
	__le32 reserved[COMPRESS_DATA_RESERVED_SIZE];	/* reserved */
	u8 cdata[];			/* compressed data */
};
#define COMPRESS_HEADER_SIZE	(sizeof(struct compress_data))
/* compress context */
struct compress_ctx {
	unsigned int cluster_size;	/* page count in cluster */
	unsigned int log_cluster_size;	/* log of cluster size */
	void *rbuf;			/* compression input buffer */
	struct compress_data *cbuf;	/* comprsssion output header + data */
	size_t rlen;			/* valid data length in rbuf */
	size_t clen;			/* valid data length in cbuf */
	void *private;			/* work buf for compress algorithm */
};

#if HAVE_BYTESWAP_H
#include <byteswap.h>
#else
/**
 * bswap_16 - reverse bytes in a uint16_t value.
 * @val: value whose bytes to swap.
 *
 * Example:
 *	// Output contains "1024 is 4 as two bytes reversed"
 *	printf("1024 is %u as two bytes reversed\n", bswap_16(1024));
 */
static inline uint16_t bswap_16(uint16_t val)
{
	return ((val & (uint16_t)0x00ffU) << 8)
		| ((val & (uint16_t)0xff00U) >> 8);
}

/**
 * bswap_32 - reverse bytes in a uint32_t value.
 * @val: value whose bytes to swap.
 *
 * Example:
 *	// Output contains "1024 is 262144 as four bytes reversed"
 *	printf("1024 is %u as four bytes reversed\n", bswap_32(1024));
 */
static inline uint32_t bswap_32(uint32_t val)
{
	return ((val & (uint32_t)0x000000ffUL) << 24)
		| ((val & (uint32_t)0x0000ff00UL) <<  8)
		| ((val & (uint32_t)0x00ff0000UL) >>  8)
		| ((val & (uint32_t)0xff000000UL) >> 24);
}
#endif /* !HAVE_BYTESWAP_H */

#if defined HAVE_DECL_BSWAP_64 && !HAVE_DECL_BSWAP_64
/**
 * bswap_64 - reverse bytes in a uint64_t value.
 * @val: value whose bytes to swap.
 *
 * Example:
 *	// Output contains "1024 is 1125899906842624 as eight bytes reversed"
 *	printf("1024 is %llu as eight bytes reversed\n",
 *		(unsigned long long)bswap_64(1024));
 */
static inline uint64_t bswap_64(uint64_t val)
{
	return ((val & (uint64_t)0x00000000000000ffULL) << 56)
		| ((val & (uint64_t)0x000000000000ff00ULL) << 40)
		| ((val & (uint64_t)0x0000000000ff0000ULL) << 24)
		| ((val & (uint64_t)0x00000000ff000000ULL) <<  8)
		| ((val & (uint64_t)0x000000ff00000000ULL) >>  8)
		| ((val & (uint64_t)0x0000ff0000000000ULL) >> 24)
		| ((val & (uint64_t)0x00ff000000000000ULL) >> 40)
		| ((val & (uint64_t)0xff00000000000000ULL) >> 56);
}
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define le16_to_cpu(x)	((uint16_t)(x))
#define le32_to_cpu(x)	((uint32_t)(x))
#define le64_to_cpu(x)	((uint64_t)(x))
#define cpu_to_le16(x)	((uint16_t)(x))
#define cpu_to_le32(x)	((uint32_t)(x))
#define cpu_to_le64(x)	((uint64_t)(x))
#define be32_to_cpu(x)	__builtin_bswap64(x)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define le16_to_cpu(x)	bswap_16(x)
#define le32_to_cpu(x)	bswap_32(x)
#define le64_to_cpu(x)	bswap_64(x)
#define cpu_to_le16(x)	bswap_16(x)
#define cpu_to_le32(x)	bswap_32(x)
#define cpu_to_le64(x)	bswap_64(x)
#define be32_to_cpu(x)	((uint64_t)(x))
#endif

#define typecheck(type,x) \
	({	type __dummy; \
		typeof(x) __dummy2; \
		(void)(&__dummy == &__dummy2); \
		1; \
	 })

#define NULL_SEGNO	((unsigned int)~0)

/*
 * Debugging interfaces
 */

#define INFO_MSG(fmt, ...)						\
	do {								\
		printf("[INFO] (%s:%4d) ", __func__, __LINE__);		\
		printf(" --> "fmt"\n", ##__VA_ARGS__);			\
	} while (0)

#define FIX_MSG(fmt, ...)						\
	do {								\
		printf("[FIX] (%s:%4d) ", __func__, __LINE__);		\
		printf(" --> "fmt"\n", ##__VA_ARGS__);			\
	} while (0)

#define ASSERT_MSG(fmt, ...)						\
	do {								\
		printf("[ASSERT] (%s:%4d) ", __func__, __LINE__);	\
		printf(" --> "fmt"\n", ##__VA_ARGS__);			\
		c.bug_on = 1;						\
	} while (0)

#define ASSERT(exp)							\
	do {								\
		if (!(exp)) {						\
			printf("[ASSERT] (%s:%4d) %s\n",		\
					__func__, __LINE__, #exp);	\
			exit(-1);					\
		}							\
	} while (0)

#define ERR_MSG(fmt, ...)						\
	do {								\
		printf("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); \
	} while (0)

#define MSG(n, fmt, ...)						\
	do {								\
		if (c.dbg_lv >= n && !c.layout && !c.show_file_map) {	\
			printf(fmt, ##__VA_ARGS__);			\
		}							\
	} while (0)

#define DBG(n, fmt, ...)						\
	do {								\
		if (c.dbg_lv >= n && !c.layout && !c.show_file_map) {	\
			printf("[%s:%4d] " fmt,				\
				__func__, __LINE__, ##__VA_ARGS__);	\
		}							\
	} while (0)

/* Display on console */
#define DISP(fmt, ptr, member)				\
	do {						\
		printf("%-30s" fmt, #member, ((ptr)->member));	\
	} while (0)

#define DISP_raw_str(fmt, member)					\
	do {								\
		if (c.layout)						\
			printf("%-30s " fmt "\n", #member":", member);	\
		else							\
			printf("%-30s" "\t\t[" fmt "]\n",		\
			#member, member);				\
	} while (0)

#define DISP_str(fmt, ptr, member)					\
	do {								\
		if (c.layout)						\
			printf("%-30s " fmt "\n",			\
			#member":", ((ptr)->member));			\
		else							\
			printf("%-30s" "\t\t[" fmt "]\n",		\
			#member, ((ptr)->member));			\
	} while (0)

#define DISP_u8(ptr, member)						\
	do {								\
		assert(sizeof((ptr)->member) == 1);			\
		if (c.layout)						\
			printf("%-30s %u\n",				\
			#member":", ((ptr)->member));			\
		else							\
			printf("%-30s" "\t\t[0x%8x : %u]\n",		\
			#member, ((ptr)->member), ((ptr)->member));	\
	} while (0)

#define DISP_u16(ptr, member)						\
	do {								\
		assert(sizeof((ptr)->member) == 2);			\
		if (c.layout)						\
			printf("%-30s %u\n",				\
			#member":", le16_to_cpu(((ptr)->member)));	\
		else							\
			printf("%-30s" "\t\t[0x%8x : %u]\n",		\
			#member, le16_to_cpu(((ptr)->member)),		\
			le16_to_cpu(((ptr)->member)));			\
	} while (0)

#define DISP_u32(ptr, member)						\
	do {								\
		assert(sizeof((ptr)->member) <= 4);			\
		if (c.layout)						\
			printf("%-30s %u\n",				\
			#member":", le32_to_cpu(((ptr)->member)));	\
		else							\
			printf("%-30s" "\t\t[0x%8x : %u]\n",		\
			#member, le32_to_cpu(((ptr)->member)),		\
			le32_to_cpu(((ptr)->member)));			\
	} while (0)

#define DISP_u64(ptr, member)						\
	do {								\
		assert(sizeof((ptr)->member) == 8);			\
		if (c.layout)						\
			printf("%-30s %" PRIu64 "\n",			\
			#member":", le64_to_cpu(((ptr)->member)));	\
		else							\
			printf("%-30s" "\t\t[0x%8" PRIx64 " : %" PRIu64 "]\n",	\
			#member, le64_to_cpu(((ptr)->member)),		\
			le64_to_cpu(((ptr)->member)));			\
	} while (0)

#define DISP_utf(ptr, member)						\
	do {								\
		if (c.layout)						\
			printf("%-30s %s\n", #member":",		\
					((ptr)->member)); 		\
		else							\
			printf("%-30s" "\t\t[%s]\n", #member,		\
					((ptr)->member));		\
	} while (0)

/* Display to buffer */
#define BUF_DISP_u32(buf, data, len, ptr, member)			\
	do {								\
		assert(sizeof((ptr)->member) <= 4);			\
		snprintf(buf, len, #member);				\
		snprintf(data, len, "0x%x : %u", ((ptr)->member),	\
						((ptr)->member));	\
	} while (0)

#define BUF_DISP_u64(buf, data, len, ptr, member)			\
	do {								\
		assert(sizeof((ptr)->member) == 8);			\
		snprintf(buf, len, #member);				\
		snprintf(data, len, "0x%llx : %llu", ((ptr)->member),	\
						((ptr)->member));	\
	} while (0)

#define BUF_DISP_utf(buf, data, len, ptr, member)			\
		snprintf(buf, len, #member)

/* these are defined in kernel */
#define BITS_PER_BYTE		8
#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT		9
#endif
#define F2FS_SUPER_MAGIC	0xF2F52010	/* F2FS Magic Number */
#define CP_CHKSUM_OFFSET	(F2FS_BLKSIZE - sizeof(__le32))
#define SB_CHKSUM_OFFSET	3068
#define MAX_PATH_LEN		64
#define MAX_DEVICES		8

#define F2FS_BYTES_TO_BLK(bytes)    ((bytes) >> F2FS_BLKSIZE_BITS)
#define F2FS_BLKSIZE_BITS c.blksize_bits

/* for mkfs */
#define	F2FS_NUMBER_OF_CHECKPOINT_PACK	2
#define	DEFAULT_SECTOR_SIZE		512
#define	DEFAULT_SECTORS_PER_BLOCK	(1 << (F2FS_BLKSIZE_BITS - SECTOR_SHIFT))
#define	DEFAULT_BLOCKS_PER_SEGMENT	512
#define DEFAULT_SEGMENTS_PER_SECTION	1
#define DEFAULT_BLKSIZE_BITS		12	/* 4096	*/

#define VERSION_LEN		256
#define VERSION_TIMESTAMP_LEN	4
#define VERSION_NAME_LEN	(VERSION_LEN - VERSION_TIMESTAMP_LEN)

#define LPF "lost+found"

/* one for gc buffer, the other for node */
#define MIN_RSVD_SECS	(NR_CURSEG_TYPE + 2U)
#define CONFIG_RSVD_DEFAULT_OP_RATIO	3.0

enum f2fs_config_func {
	MKFS,
	FSCK,
	DUMP,
	DEFRAG,
	RESIZE,
	SLOAD,
	LABEL,
	INJECT,
};

enum default_set {
	CONF_NONE = 0,
	CONF_ANDROID,
};

struct device_info {
	char *path;
	int32_t fd;
	uint32_t sector_size;
	uint64_t total_sectors;	/* got by get_device_info */
	uint64_t start_blkaddr;
	uint64_t end_blkaddr;
	uint32_t total_segments;
	char *alias_filename;

	/* to handle zone block devices */
	int zoned_model;
	uint32_t nr_zones;
	uint32_t nr_rnd_zones;
	size_t zone_blocks;
	uint64_t zone_size;
	size_t *zone_cap_blocks;
};

typedef struct {
	/* Value 0 means no cache, minimum 1024 */
	long num_cache_entry;

	/* Value 0 means always overwrite (no collision allowed). maximum 16 */
	unsigned max_hash_collision;

	bool dbg_en;
} dev_cache_config_t;

/* f2fs_configration for compression used for sload.f2fs */
typedef struct  {
	void (*init)(struct compress_ctx *cc);
	int (*compress)(struct compress_ctx *cc);
	void (*reset)(struct compress_ctx *cc);
} compress_ops;

/* Should be aligned to supported_comp_names and support_comp_ops */
enum compress_algorithms {
	COMPR_LZO,
	COMPR_LZ4,
	MAX_COMPRESS_ALGS,
};

enum filter_policy {
	COMPR_FILTER_UNASSIGNED = 0,
	COMPR_FILTER_ALLOW,
	COMPR_FILTER_DENY,
};

typedef struct {
	void (*add)(const char *);
	void (*destroy)(void);
	bool (*filter)(const char *);
} filter_ops;

typedef struct {
	bool enabled;			/* disabled by default */
	bool required;			/* require to enable */
	bool readonly;			/* readonly to release blocks */
	struct compress_ctx cc;		/* work context */
	enum compress_algorithms alg;	/* algorithm to compress */
	compress_ops *ops;		/* ops per algorithm */
	unsigned int min_blocks;	/* save more blocks than this */
	enum filter_policy filter;	/* filter to try compression */
	filter_ops *filter_ops;		/* filter ops */
} compress_config_t;

#define ALIGN_DOWN(addrs, size)	(((addrs) / (size)) * (size))
#define ALIGN_UP(addrs, size)	ALIGN_DOWN(((addrs) + (size) - 1), (size))

#ifdef CONFIG_64BIT
#define BITS_PER_LONG	64
#else
#define BITS_PER_LONG	32
#endif

#define BIT_MASK(nr)	(1 << (nr % BITS_PER_LONG))
#define BIT_WORD(nr)	(nr / BITS_PER_LONG)

#define set_sb_le64(member, val)		(sb->member = cpu_to_le64(val))
#define set_sb_le32(member, val)		(sb->member = cpu_to_le32(val))
#define set_sb_le16(member, val)		(sb->member = cpu_to_le16(val))
#define get_sb_le64(member)			le64_to_cpu(sb->member)
#define get_sb_le32(member)			le32_to_cpu(sb->member)
#define get_sb_le16(member)			le16_to_cpu(sb->member)
#define get_newsb_le64(member)			le64_to_cpu(new_sb->member)
#define get_newsb_le32(member)			le32_to_cpu(new_sb->member)
#define get_newsb_le16(member)			le16_to_cpu(new_sb->member)

#define set_sb(member, val)						\
			do {						\
				typeof(sb->member) t = (val);		\
				switch (sizeof(t)) {			\
				case 8: set_sb_le64(member, t); break;	\
				case 4: set_sb_le32(member, t); break;	\
				case 2: set_sb_le16(member, t); break;	\
				}					\
			} while(0)

#define get_sb(member)		\
			({						\
				typeof(sb->member) t;			\
				switch (sizeof(t)) {			\
				case 8: t = get_sb_le64(member); break; \
				case 4: t = get_sb_le32(member); break; \
				case 2: t = get_sb_le16(member); break; \
				} 					\
				t; \
			})
#define get_newsb(member)		\
			({						\
				typeof(new_sb->member) t;		\
				switch (sizeof(t)) {			\
				case 8: t = get_newsb_le64(member); break; \
				case 4: t = get_newsb_le32(member); break; \
				case 2: t = get_newsb_le16(member); break; \
				} 					\
				t; \
			})

#define set_cp_le64(member, val)		(cp->member = cpu_to_le64(val))
#define set_cp_le32(member, val)		(cp->member = cpu_to_le32(val))
#define set_cp_le16(member, val)		(cp->member = cpu_to_le16(val))
#define get_cp_le64(member)			le64_to_cpu(cp->member)
#define get_cp_le32(member)			le32_to_cpu(cp->member)
#define get_cp_le16(member)			le16_to_cpu(cp->member)

#define set_cp(member, val)						\
			do {						\
				typeof(cp->member) t = (val);		\
				switch (sizeof(t)) {			\
				case 8: set_cp_le64(member, t); break;	\
				case 4: set_cp_le32(member, t); break;	\
				case 2: set_cp_le16(member, t); break;	\
				}					\
			} while(0)

#define get_cp(member)		\
			({						\
				typeof(cp->member) t;			\
				switch (sizeof(t)) {			\
				case 8: t = get_cp_le64(member); break; \
				case 4: t = get_cp_le32(member); break; \
				case 2: t = get_cp_le16(member); break; \
				} 					\
				t; \
			})

/*
 * Copied from include/linux/kernel.h
 */
#define __round_mask(x, y)	((__typeof__(x))((y)-1))
#define round_down(x, y)	((x) & ~__round_mask(x, y))

#define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })

#define max(x, y) ({				\
	typeof(x) _max1 = (x);			\
	typeof(y) _max2 = (y);			\
	(void) (&_max1 == &_max2);		\
	_max1 > _max2 ? _max1 : _max2; })

#define round_up(x, y)		(((x) + (y) - 1) / (y))
/*
 * Copied from fs/f2fs/f2fs.h
 */
#define	NR_CURSEG_DATA_TYPE	(3)
#define NR_CURSEG_NODE_TYPE	(3)
#define NR_CURSEG_TYPE	(NR_CURSEG_DATA_TYPE + NR_CURSEG_NODE_TYPE)

enum {
	CURSEG_HOT_DATA	= 0,	/* directory entry blocks */
	CURSEG_WARM_DATA,	/* data blocks */
	CURSEG_COLD_DATA,	/* multimedia or GCed data blocks */
	CURSEG_HOT_NODE,	/* direct node blocks of directory files */
	CURSEG_WARM_NODE,	/* direct node blocks of normal files */
	CURSEG_COLD_NODE,	/* indirect node blocks */
	NO_CHECK_TYPE
};

enum {
	CURSEG_RO_HOT_DATA,
	CURSEG_RO_HOT_NODE,
	NR_RO_CURSEG_TYPE,
};

#define F2FS_MIN_SEGMENTS	9 /* SB + 2 (CP + SIT + NAT) + SSA + MAIN */

/*
 * Copied from fs/f2fs/segment.h
 */
#define GET_SUM_TYPE(sum) (F2FS_SUMMARY_BLOCK_FOOTER(sum)->entry_type)
#define SET_SUM_TYPE(sum, type) (F2FS_SUMMARY_BLOCK_FOOTER(sum)->entry_type = type)

/*
 * Copied from include/linux/f2fs_sb.h
 */
#define F2FS_SUPER_OFFSET		1024	/* byte-size offset */
#define F2FS_MIN_LOG_SECTOR_SIZE	9	/* 9 bits for 512 bytes */
#define F2FS_MAX_LOG_SECTOR_SIZE	F2FS_BLKSIZE_BITS	/* max sector size is block size */
#define F2FS_MIN_BLKSIZE		4096
#define F2FS_MAX_BLKSIZE		16384
#define F2FS_BLKSIZE			c.blksize	/* support configurable block size */
#define F2FS_MAX_EXTENSION		64	/* # of extension entries */
#define F2FS_EXTENSION_LEN		8	/* max size of extension */
#define F2FS_BLK_ALIGN(x)	(((x) + F2FS_BLKSIZE - 1) / F2FS_BLKSIZE)

#define NULL_ADDR		0x0U
#define NEW_ADDR		-1U
#define COMPRESS_ADDR		-2U

#define F2FS_ROOT_INO(sbi)	(sbi->root_ino_num)
#define F2FS_NODE_INO(sbi)	(sbi->node_ino_num)
#define F2FS_META_INO(sbi)	(sbi->meta_ino_num)

#define F2FS_MAX_QUOTAS		3
#define QUOTA_DATA		(((1024 * 6 - 1) / F2FS_BLKSIZE) + 1)
#define QUOTA_INO(sb,t)	(le32_to_cpu((sb)->qf_ino[t]))

/*
 * On-disk inode flags (f2fs_inode::i_flags)
 */
#define F2FS_COMPR_FL			0x00000004 /* Compress file */
#define F2FS_NODUMP_FL			0x00000040 /* do not dump file */
#define F2FS_IMMUTABLE_FL		0x00000010 /* Immutable file */
#define F2FS_NOATIME_FL			0x00000080 /* do not update atime */
#define F2FS_CASEFOLD_FL		0x40000000 /* Casefolded file */
#define F2FS_DEVICE_ALIAS_FL		0x80000000 /* File for aliasing a device */
#define IS_DEVICE_ALIASING(fi)	((fi)->i_flags & cpu_to_le32(F2FS_DEVICE_ALIAS_FL))

#define F2FS_ENC_UTF8_12_1	1
#define F2FS_ENC_STRICT_MODE_FL		(1 << 0)
#define F2FS_ENC_NO_COMPAT_FALLBACK_FL	(1 << 1)

/* This flag is used by node and meta inodes, and by recovery */
#define GFP_F2FS_ZERO	(GFP_NOFS | __GFP_ZERO)

/*
 * For further optimization on multi-head logs, on-disk layout supports maximum
 * 16 logs by default. The number, 16, is expected to cover all the cases
 * enoughly. The implementaion currently uses no more than 6 logs.
 * Half the logs are used for nodes, and the other half are used for data.
 */
#define MAX_ACTIVE_LOGS	16
#define MAX_ACTIVE_NODE_LOGS	8
#define MAX_ACTIVE_DATA_LOGS	8

#define F2FS_FEATURE_ENCRYPT		0x0001
#define F2FS_FEATURE_BLKZONED		0x0002
#define F2FS_FEATURE_ATOMIC_WRITE	0x0004
#define F2FS_FEATURE_EXTRA_ATTR		0x0008
#define F2FS_FEATURE_PRJQUOTA		0x0010
#define F2FS_FEATURE_INODE_CHKSUM	0x0020
#define F2FS_FEATURE_FLEXIBLE_INLINE_XATTR	0x0040
#define F2FS_FEATURE_QUOTA_INO		0x0080
#define F2FS_FEATURE_INODE_CRTIME	0x0100
#define F2FS_FEATURE_LOST_FOUND		0x0200
#define F2FS_FEATURE_VERITY		0x0400	/* reserved */
#define F2FS_FEATURE_SB_CHKSUM		0x0800
#define F2FS_FEATURE_CASEFOLD		0x1000
#define F2FS_FEATURE_COMPRESSION	0x2000
#define F2FS_FEATURE_RO			0x4000
#define F2FS_FEATURE_DEVICE_ALIAS	0x8000

#define MAX_NR_FEATURE			32

#define MAX_VOLUME_NAME		512

/*
 * For superblock
 */
struct f2fs_device {
	__u8 path[MAX_PATH_LEN];
	__le32 total_segments;
};

static_assert(sizeof(struct f2fs_device) == 68, "");

/* reason of stop_checkpoint */
enum stop_cp_reason {
	STOP_CP_REASON_SHUTDOWN,
	STOP_CP_REASON_FAULT_INJECT,
	STOP_CP_REASON_META_PAGE,
	STOP_CP_REASON_WRITE_FAIL,
	STOP_CP_REASON_CORRUPTED_SUMMARY,
	STOP_CP_REASON_UPDATE_INODE,
	STOP_CP_REASON_FLUSH_FAIL,
	STOP_CP_REASON_NO_SEGMENT,
	STOP_CP_REASON_CORRUPTED_FREE_BITMAP,
	STOP_CP_REASON_MAX,
};

#define	MAX_STOP_REASON			32

/* detail reason for EFSCORRUPTED */
enum f2fs_error {
	ERROR_CORRUPTED_CLUSTER,
	ERROR_FAIL_DECOMPRESSION,
	ERROR_INVALID_BLKADDR,
	ERROR_CORRUPTED_DIRENT,
	ERROR_CORRUPTED_INODE,
	ERROR_INCONSISTENT_SUMMARY,
	ERROR_INCONSISTENT_FOOTER,
	ERROR_INCONSISTENT_SUM_TYPE,
	ERROR_CORRUPTED_JOURNAL,
	ERROR_INCONSISTENT_NODE_COUNT,
	ERROR_INCONSISTENT_BLOCK_COUNT,
	ERROR_INVALID_CURSEG,
	ERROR_INCONSISTENT_SIT,
	ERROR_CORRUPTED_VERITY_XATTR,
	ERROR_CORRUPTED_XATTR,
	ERROR_INVALID_NODE_REFERENCE,
	ERROR_INCONSISTENT_NAT,
	ERROR_MAX,
};

#define MAX_F2FS_ERRORS			16

struct f2fs_super_block {
	__le32 magic;			/* Magic Number */
	__le16 major_ver;		/* Major Version */
	__le16 minor_ver;		/* Minor Version */
	__le32 log_sectorsize;		/* log2 sector size in bytes */
	__le32 log_sectors_per_block;	/* log2 # of sectors per block */
	__le32 log_blocksize;		/* log2 block size in bytes */
	__le32 log_blocks_per_seg;	/* log2 # of blocks per segment */
	__le32 segs_per_sec;		/* # of segments per section */
	__le32 secs_per_zone;		/* # of sections per zone */
	__le32 checksum_offset;		/* checksum offset inside super block */
	__le64 block_count __attribute__((packed));
					/* total # of user blocks */
	__le32 section_count;		/* total # of sections */
	__le32 segment_count;		/* total # of segments */
	__le32 segment_count_ckpt;	/* # of segments for checkpoint */
	__le32 segment_count_sit;	/* # of segments for SIT */
	__le32 segment_count_nat;	/* # of segments for NAT */
	__le32 segment_count_ssa;	/* # of segments for SSA */
	__le32 segment_count_main;	/* # of segments for main area */
	__le32 segment0_blkaddr;	/* start block address of segment 0 */
	__le32 cp_blkaddr;		/* start block address of checkpoint */
	__le32 sit_blkaddr;		/* start block address of SIT */
	__le32 nat_blkaddr;		/* start block address of NAT */
	__le32 ssa_blkaddr;		/* start block address of SSA */
	__le32 main_blkaddr;		/* start block address of main area */
	__le32 root_ino;		/* root inode number */
	__le32 node_ino;		/* node inode number */
	__le32 meta_ino;		/* meta inode number */
	__u8 uuid[16];			/* 128-bit uuid for volume */
	__le16 volume_name[MAX_VOLUME_NAME];	/* volume name */
	__le32 extension_count;		/* # of extensions below */
	__u8 extension_list[F2FS_MAX_EXTENSION][8];	/* extension array */
	__le32 cp_payload;
	__u8 version[VERSION_LEN];	/* the kernel version */
	__u8 init_version[VERSION_LEN];	/* the initial kernel version */
	__le32 feature;			/* defined features */
	__u8 encryption_level;		/* versioning level for encryption */
	__u8 encrypt_pw_salt[16];	/* Salt used for string2key algorithm */
	struct f2fs_device devs[MAX_DEVICES] __attribute__((packed));	/* device list */
	__le32 qf_ino[F2FS_MAX_QUOTAS] __attribute__((packed));	/* quota inode numbers */
	__u8 hot_ext_count;		/* # of hot file extension */
	__le16  s_encoding;		/* Filename charset encoding */
	__le16  s_encoding_flags;	/* Filename charset encoding flags */
	__u8 s_stop_reason[MAX_STOP_REASON];	/* stop checkpoint reason */
	__u8 s_errors[MAX_F2FS_ERRORS];		/* reason of image corrupts */
	__u8 reserved[258];		/* valid reserved region */
	__le32 crc;			/* checksum of superblock */
} __attribute__((packed));

static_assert(sizeof(struct f2fs_super_block) == 3072, "");

/*
 * For checkpoint
 */
#define CP_RESIZEFS_FLAG                0x00004000
#define CP_DISABLED_FLAG		0x00001000
#define CP_QUOTA_NEED_FSCK_FLAG		0x00000800
#define CP_LARGE_NAT_BITMAP_FLAG	0x00000400
#define CP_NOCRC_RECOVERY_FLAG	0x00000200
#define CP_TRIMMED_FLAG		0x00000100
#define CP_NAT_BITS_FLAG	0x00000080
#define CP_CRC_RECOVERY_FLAG	0x00000040
#define CP_FASTBOOT_FLAG	0x00000020
#define CP_FSCK_FLAG		0x00000010
#define CP_ERROR_FLAG		0x00000008
#define CP_COMPACT_SUM_FLAG	0x00000004
#define CP_ORPHAN_PRESENT_FLAG	0x00000002
#define CP_UMOUNT_FLAG		0x00000001

#define F2FS_CP_PACKS		2	/* # of checkpoint packs */

struct f2fs_checkpoint {
	__le64 checkpoint_ver;		/* checkpoint block version number */
	__le64 user_block_count;	/* # of user blocks */
	__le64 valid_block_count;	/* # of valid blocks in main area */
	__le32 rsvd_segment_count;	/* # of reserved segments for gc */
	__le32 overprov_segment_count;	/* # of overprovision segments */
	__le32 free_segment_count;	/* # of free segments in main area */

	/* information of current node segments */
	__le32 cur_node_segno[MAX_ACTIVE_NODE_LOGS];
	__le16 cur_node_blkoff[MAX_ACTIVE_NODE_LOGS];
	/* information of current data segments */
	__le32 cur_data_segno[MAX_ACTIVE_DATA_LOGS];
	__le16 cur_data_blkoff[MAX_ACTIVE_DATA_LOGS];
	__le32 ckpt_flags;		/* Flags : umount and journal_present */
	__le32 cp_pack_total_block_count;	/* total # of one cp pack */
	__le32 cp_pack_start_sum;	/* start block number of data summary */
	__le32 valid_node_count;	/* Total number of valid nodes */
	__le32 valid_inode_count;	/* Total number of valid inodes */
	__le32 next_free_nid;		/* Next free node number */
	__le32 sit_ver_bitmap_bytesize;	/* Default value 64 */
	__le32 nat_ver_bitmap_bytesize; /* Default value 256 */
	__le32 checksum_offset;		/* checksum offset inside cp block */
	__le64 elapsed_time;		/* mounted time */
	/* allocation type of current segment */
	unsigned char alloc_type[MAX_ACTIVE_LOGS];

	/* SIT and NAT version bitmap */
	unsigned char sit_nat_version_bitmap[];
};

static_assert(sizeof(struct f2fs_checkpoint) == 192, "");

#define CP_BITMAP_OFFSET	\
	(offsetof(struct f2fs_checkpoint, sit_nat_version_bitmap))
#define CP_MIN_CHKSUM_OFFSET	CP_BITMAP_OFFSET

#define MIN_NAT_BITMAP_SIZE	64
#define MAX_SIT_BITMAP_SIZE_IN_CKPT    \
	(CP_CHKSUM_OFFSET - CP_BITMAP_OFFSET - MIN_NAT_BITMAP_SIZE)
#define MAX_BITMAP_SIZE_IN_CKPT	\
	(CP_CHKSUM_OFFSET - CP_BITMAP_OFFSET)

/*
 * For orphan inode management
 */
#define F2FS_ORPHANS_PER_BLOCK	((F2FS_BLKSIZE - 4 * sizeof(__le32)) / sizeof(__le32))

/*
 * On disk layout is:
 * __le32 ino[F2FS_ORPHANS_PER_BLOCK];
 * struct f2fs_ophan_block_footer
 *
 * Do NOT use sizeof, use F2FS_BLKSIZE instead
 */
struct f2fs_orphan_block {
	__le32 ino[0];	/* F2FS_ORPHANS_PER_BLOCK inode numbers */
};
#define F2FS_ORPHAN_BLOCK_FOOTER(blk) ((struct orphan_block_footer *)&(blk)->ino[F2FS_ORPHANS_PER_BLOCK])

struct orphan_block_footer {
	__le32 reserved;	/* reserved */
	__le16 blk_addr;	/* block index in current CP */
	__le16 blk_count;	/* Number of orphan inode blocks in CP */
	__le32 entry_count;	/* Total number of orphan nodes in current CP */
	__le32 check_sum;	/* CRC32 for orphan inode block */
};

/*
 * For NODE structure
 */
struct f2fs_extent {
	__le32 fofs;		/* start file offset of the extent */
	__le32 blk_addr;	/* start block address of the extent */
	__le32 len;		/* lengh of the extent */
};

static_assert(sizeof(struct f2fs_extent) == 12, "");

#define F2FS_NAME_LEN		255

/* max output length of pretty_print_filename() including null terminator */
#define F2FS_PRINT_NAMELEN	(4 * ((F2FS_NAME_LEN + 2) / 3) + 1)

/* 200 bytes for inline xattrs by default */
#define DEFAULT_INLINE_XATTR_ADDRS	50

struct node_footer {
	__le32 nid;   /* node id */
	__le32 ino;   /* inode number */
	__le32 flag;    /* include cold/fsync/dentry marks and offset */
	__le64 cp_ver __attribute__((packed));    /* checkpoint version */
	__le32 next_blkaddr;  /* next node page block address */
};

static_assert(sizeof(struct node_footer) == 24, "");

#define OFFSET_OF_END_OF_I_EXT    360
#define SIZE_OF_I_NID     20
/* Address Pointers in an Inode */
#define DEF_ADDRS_PER_INODE ((F2FS_BLKSIZE - OFFSET_OF_END_OF_I_EXT \
				- SIZE_OF_I_NID \
				- sizeof(struct node_footer)) / sizeof(__le32))
#define CUR_ADDRS_PER_INODE(inode)	(DEF_ADDRS_PER_INODE - \
					__get_extra_isize(inode))
#define ADDRS_PER_INODE(i)	addrs_per_page(i, true)
/* Address Pointers in a Direct Block */
#define DEF_ADDRS_PER_BLOCK ((F2FS_BLKSIZE - sizeof(struct node_footer)) / sizeof(__le32))
#define ADDRS_PER_BLOCK(i)	addrs_per_page(i, false)
/* Node IDs in an Indirect Block */
#define NIDS_PER_BLOCK    ((F2FS_BLKSIZE - sizeof(struct node_footer)) / sizeof(__le32))

#define	NODE_DIR1_BLOCK		(DEF_ADDRS_PER_INODE + 1)
#define	NODE_DIR2_BLOCK		(DEF_ADDRS_PER_INODE + 2)
#define	NODE_IND1_BLOCK		(DEF_ADDRS_PER_INODE + 3)
#define	NODE_IND2_BLOCK		(DEF_ADDRS_PER_INODE + 4)
#define	NODE_DIND_BLOCK		(DEF_ADDRS_PER_INODE + 5)

#define F2FS_INLINE_XATTR	0x01	/* file inline xattr flag */
#define F2FS_INLINE_DATA	0x02	/* file inline data flag */
#define F2FS_INLINE_DENTRY	0x04	/* file inline dentry flag */
#define F2FS_DATA_EXIST		0x08	/* file inline data exist flag */
#define F2FS_INLINE_DOTS	0x10	/* file having implicit dot dentries */
#define F2FS_EXTRA_ATTR		0x20	/* file having extra attribute */
#define F2FS_PIN_FILE		0x40	/* file should not be gced */
#define F2FS_COMPRESS_RELEASED	0x80	/* file released compressed blocks */

#define F2FS_EXTRA_ISIZE_OFFSET				\
	offsetof(struct f2fs_inode, i_extra_isize)
#define F2FS_TOTAL_EXTRA_ATTR_SIZE			\
	(offsetof(struct f2fs_inode, i_extra_end) - F2FS_EXTRA_ISIZE_OFFSET)

#define	F2FS_DEF_PROJID		0	/* default project ID */

#define MAX_INLINE_DATA(node) (sizeof(__le32) *				\
				(DEF_ADDRS_PER_INODE -			\
				get_inline_xattr_addrs(&node->i) -	\
				get_extra_isize(node) -			\
				DEF_INLINE_RESERVED_SIZE))
#define DEF_MAX_INLINE_DATA	(sizeof(__le32) *			\
				(DEF_ADDRS_PER_INODE -			\
				DEFAULT_INLINE_XATTR_ADDRS -		\
				F2FS_TOTAL_EXTRA_ATTR_SIZE -		\
				DEF_INLINE_RESERVED_SIZE))
#define INLINE_DATA_OFFSET	(F2FS_BLKSIZE -				\
				sizeof(struct node_footer) -		\
				sizeof(__le32) * (DEF_ADDRS_PER_INODE +	\
				5 - DEF_INLINE_RESERVED_SIZE))

#define DEF_DIR_LEVEL		0

/*
 * i_advise uses FADVISE_XXX_BIT. We can add additional hints later.
 */
#define FADVISE_COLD_BIT	0x01
#define FADVISE_LOST_PINO_BIT	0x02
#define FADVISE_ENCRYPT_BIT	0x04
#define FADVISE_ENC_NAME_BIT	0x08
#define FADVISE_KEEP_SIZE_BIT	0x10
#define FADVISE_HOT_BIT		0x20
#define FADVISE_VERITY_BIT	0x40	/* reserved */

#define file_is_encrypt(fi)      ((fi)->i_advise & FADVISE_ENCRYPT_BIT)
#define file_enc_name(fi)        ((fi)->i_advise & FADVISE_ENC_NAME_BIT)
#define IS_CASEFOLDED(dir)     ((dir)->i_flags & cpu_to_le32(F2FS_CASEFOLD_FL))

/*
 * fsck i_compr_blocks counting helper
 */
struct f2fs_compr_blk_cnt {
	/* counting i_compr_blocks, init 0 */
	u32 cnt;

	/*
	 * previous seen compression header (COMPR_ADDR) page offsets,
	 * use CHEADER_PGOFS_NONE for none
	 */
	u32 cheader_pgofs;
};
#define CHEADER_PGOFS_NONE ((u32)-(1 << MAX_COMPRESS_LOG_SIZE))

/*
 * On disk layout is
 * struct f2fs_inode
 * struct f2fs_inode_nids
 *
 * where the size of f2fs_inode depends on block size.
 * Do NOT use sizeof
 */
struct f2fs_inode {
	__le16 i_mode;			/* file mode */
	__u8 i_advise;			/* file hints */
	__u8 i_inline;			/* file inline flags */
	__le32 i_uid;			/* user ID */
	__le32 i_gid;			/* group ID */
	__le32 i_links;			/* links count */
	__le64 i_size;			/* file size in bytes */
	__le64 i_blocks;		/* file size in blocks */
	__le64 i_atime;			/* access time */
	__le64 i_ctime;			/* change time */
	__le64 i_mtime;			/* modification time */
	__le32 i_atime_nsec;		/* access time in nano scale */
	__le32 i_ctime_nsec;		/* change time in nano scale */
	__le32 i_mtime_nsec;		/* modification time in nano scale */
	__le32 i_generation;		/* file version (for NFS) */
	union {
		__le32 i_current_depth;	/* only for directory depth */
		__le16 i_gc_failures;	/*
					 * # of gc failures on pinned file.
					 * only for regular files.
					 */
	};
	__le32 i_xattr_nid;		/* nid to save xattr */
	__le32 i_flags;			/* file attributes */
	__le32 i_pino;			/* parent inode number */
	__le32 i_namelen;		/* file name length */
	__u8 i_name[F2FS_NAME_LEN];	/* file name for SPOR */
	__u8 i_dir_level;		/* dentry_level for large dir */

	struct f2fs_extent i_ext __attribute__((packed));	/* caching a largest extent */

	union {
		struct {
			__le16 i_extra_isize;	/* extra inode attribute size */
			__le16 i_inline_xattr_size;	/* inline xattr size, unit: 4 bytes */
			__le32 i_projid;	/* project id */
			__le32 i_inode_checksum;/* inode meta checksum */
			__le64 i_crtime;	/* creation time */
			__le32 i_crtime_nsec;	/* creation time in nano scale */
			__le64 i_compr_blocks;	/* # of compressed blocks */
			__u8 i_compress_algorithm;	/* compress algorithm */
			__u8 i_log_cluster_size;	/* log of cluster size */
			__le16 i_compress_flag;		/* compress flag */
						/* 0 bit: chksum flag
						 * [8,15] bits: compress level
						 */
			__le32 i_extra_end[0];	/* for attribute size calculation */
		} __attribute__((packed));
		__le32 i_addr[0];	/* Pointers to DEF_ADDRS_PER_INODE data blocks */
	};
};
struct f2fs_inode_nids {
	__le32 i_nid[5];		/* direct(2), indirect(2),
						double_indirect(1) node id */
};

#define F2FS_INODE_NIDS(inode) ((struct f2fs_inode_nids *)(&(inode)->i_addr[DEF_ADDRS_PER_INODE]))
#define F2FS_INODE_I_NID(inode, i) (F2FS_INODE_NIDS((inode))->i_nid[(i)])

static_assert(offsetof(struct f2fs_inode, i_extra_end) -
	      offsetof(struct f2fs_inode, i_extra_isize) == 36, "");

struct direct_node {
	__le32 addr[0];	/* array of DEF_ADDRS_PER_BLOCK data block address */
};

struct indirect_node {
	__le32 nid[0];	/* array of NIDS_PER_BLOCK data block address */
};

enum {
	COLD_BIT_SHIFT = 0,
	FSYNC_BIT_SHIFT,
	DENT_BIT_SHIFT,
	OFFSET_BIT_SHIFT
};

#define XATTR_NODE_OFFSET	((((unsigned int)-1) << OFFSET_BIT_SHIFT) \
				>> OFFSET_BIT_SHIFT)
/*
 * On disk format is:
 * struct f2fs_node
 * struct node_footer
 *
 * where the size of f2fs_node depends on Block Size.
 * Do NOT use sizeof. Use F2FS_BLKSIZE instead.
 */
struct f2fs_node {
	/* can be one of three types: inode, direct, and indirect types */
	union {
		struct f2fs_inode i;
		struct direct_node dn;
		struct indirect_node in;
	};
};
#define F2FS_NODE_FOOTER(blk) ((struct node_footer *)\
				&(((char *)(&(blk)->i))[F2FS_BLKSIZE - sizeof(struct node_footer)]))

/*
 * For NAT entries
 */
#define NAT_ENTRY_PER_BLOCK (F2FS_BLKSIZE / sizeof(struct f2fs_nat_entry))
#define NAT_BLOCK_OFFSET(start_nid) (start_nid / NAT_ENTRY_PER_BLOCK)

#define DEFAULT_NAT_ENTRY_RATIO		20

struct f2fs_nat_entry {
	__u8 version;		/* latest version of cached nat entry */
	__le32 ino;		/* inode number */
	__le32 block_addr;	/* block address */
} __attribute__((packed));

static_assert(sizeof(struct f2fs_nat_entry) == 9, "");

struct f2fs_nat_block {
	struct f2fs_nat_entry entries[0]; /* NAT_ENTRY_PER_BLOCK */
};

/*
 * For SIT entries
 *
 * Each segment is 2MB in size by default so that a bitmap for validity of
 * there-in blocks should occupy 64 bytes, 512 bits.
 * Not allow to change this.
 */
#define SIT_VBLOCK_MAP_SIZE 64
#define SIT_ENTRY_PER_BLOCK (F2FS_BLKSIZE / sizeof(struct f2fs_sit_entry))

/*
 * F2FS uses 4 bytes to represent block address. As a result, supported size of
 * disk is 16 TB and it equals to 16 * 1024 * 1024 / 2 segments.
 */
#define F2FS_MIN_SEGMENT      9 /* SB + 2 (CP + SIT + NAT) + SSA + MAIN */
#define F2FS_MAX_SEGMENT       ((16 * 1024 * 1024) / 2)
#define MAX_SIT_BITMAP_SIZE    (SEG_ALIGN(SIZE_ALIGN(F2FS_MAX_SEGMENT, \
						SIT_ENTRY_PER_BLOCK)) * \
						c.blks_per_seg / 8)
#define MAX_CP_PAYLOAD         (SEG_ALIGN(SIZE_ALIGN(UINT32_MAX, NAT_ENTRY_PER_BLOCK)) * \
						DEFAULT_NAT_ENTRY_RATIO / 100 * \
						c.blks_per_seg / 8 + \
						MAX_SIT_BITMAP_SIZE - MAX_BITMAP_SIZE_IN_CKPT)

/*
 * Note that f2fs_sit_entry->vblocks has the following bit-field information.
 * [15:10] : allocation type such as CURSEG_XXXX_TYPE
 * [9:0] : valid block count
 */
#define SIT_VBLOCKS_SHIFT	10
#define SIT_VBLOCKS_MASK	((1 << SIT_VBLOCKS_SHIFT) - 1)
#define GET_SIT_VBLOCKS(raw_sit)				\
	(le16_to_cpu((raw_sit)->vblocks) & SIT_VBLOCKS_MASK)
#define GET_SIT_TYPE(raw_sit)					\
	((le16_to_cpu((raw_sit)->vblocks) & ~SIT_VBLOCKS_MASK)	\
	 >> SIT_VBLOCKS_SHIFT)

struct f2fs_sit_entry {
	__le16 vblocks;				/* reference above */
	__u8 valid_map[SIT_VBLOCK_MAP_SIZE];	/* bitmap for valid blocks */
	__le64 mtime;				/* segment age for cleaning */
} __attribute__((packed));

static_assert(sizeof(struct f2fs_sit_entry) == 74, "");

/*
 * On disk layout is:
 * struct f2fs_sit_entry entries[SIT_ENTRY_PER_BLOCK];
 */
struct f2fs_sit_block {
	struct f2fs_sit_entry entries[0];
};

/*
 * For segment summary
 *
 * One summary block contains exactly 2048 summary entries, which represents
 * exactly 32MB segment by default. Not allow to change the basic units.
 *
 * NOTE: For initializing fields, you must use set_summary
 *
 * - If data page, nid represents dnode's nid
 * - If node page, nid represents the node page's nid.
 *
 * The ofs_in_node is used by only data page. It represents offset
 * from node's page's beginning to get a data block address.
 * ex) data_blkaddr = (block_t)(nodepage_start_address + ofs_in_node)
 */
#define ENTRIES_IN_SUM		(F2FS_BLKSIZE / 8)
#define	SUMMARY_SIZE		(7)	/* sizeof(struct summary) */
#define	SUM_FOOTER_SIZE		(5)	/* sizeof(struct summary_footer) */
#define SUM_ENTRIES_SIZE	(SUMMARY_SIZE * ENTRIES_IN_SUM)

/* a summary entry for a 4KB-sized block in a segment */
struct f2fs_summary {
	__le32 nid;		/* parent node id */
	union {
		__u8 reserved[3];
		struct {
			__u8 version;		/* node version number */
			__le16 ofs_in_node;	/* block index in parent node */
		} __attribute__((packed));
	};
} __attribute__((packed));

static_assert(sizeof(struct f2fs_summary) == 7, "");

/* summary block type, node or data, is stored to the summary_footer */
#define SUM_TYPE_NODE		(1)
#define SUM_TYPE_DATA		(0)

struct summary_footer {
	unsigned char entry_type;	/* SUM_TYPE_XXX */
	__le32 check_sum __attribute__((packed)); /* summary checksum */
};

static_assert(sizeof(struct summary_footer) == 5, "");

#define SUM_JOURNAL_SIZE	(F2FS_BLKSIZE - SUM_FOOTER_SIZE -\
				SUM_ENTRIES_SIZE)
#define NAT_JOURNAL_ENTRIES	((SUM_JOURNAL_SIZE - 2) /\
				sizeof(struct nat_journal_entry))
#define NAT_JOURNAL_RESERVED	((SUM_JOURNAL_SIZE - 2) %\
				sizeof(struct nat_journal_entry))
#define SIT_JOURNAL_ENTRIES	((SUM_JOURNAL_SIZE - 2) /\
				sizeof(struct sit_journal_entry))
#define SIT_JOURNAL_RESERVED	((SUM_JOURNAL_SIZE - 2) %\
				sizeof(struct sit_journal_entry))

/*
 * Reserved area should make size of f2fs_extra_info equals to
 * that of nat_journal and sit_journal.
 */
#define EXTRA_INFO_RESERVED	(SUM_JOURNAL_SIZE - 2 - 8)

/*
 * frequently updated NAT/SIT entries can be stored in the spare area in
 * summary blocks
 */
enum {
	NAT_JOURNAL = 0,
	SIT_JOURNAL
};

struct nat_journal_entry {
	__le32 nid;
	struct f2fs_nat_entry ne;
} __attribute__((packed));

static_assert(sizeof(struct nat_journal_entry) == 13, "");

/*
 * Layout is as follows:
 * struct nat_journal_entry entries[NAT_JOURNAL_ENTRIES];
 * __u8 reserved[NAT_JOURNAL_RESERVED];
 */
struct nat_journal {
	struct nat_journal_entry entries[0];
};

struct sit_journal_entry {
	__le32 segno;
	struct f2fs_sit_entry se;
} __attribute__((packed));

static_assert(sizeof(struct sit_journal_entry) == 78, "");

/*
 * Layout is as follows:
 * struct sit_journal_entry entries[SIT_JOURNAL_ENTRIES];
 * __u8 reserved[SIT_JOURNAL_RESERVED];
 */
struct sit_journal {
	struct sit_journal_entry entries[0];
};

/*
 * Layout is as follows:
 * __le64 kbytes_written;
 * __u8 reserved[EXTRA_INFO_RESERVED];
 */
struct f2fs_extra_info {
	__le64 kbytes_written;
	__u8 reserved[0];
} __attribute__((packed));

/*
 * This struct's used size depends on F2FS_BLKSIZE. DO NOT use sizeof
 */
struct f2fs_journal {
	union {
		__le16 n_nats;
		__le16 n_sits;
	};
	/* spare area is used by NAT or SIT journals or extra info */
	union {
		struct nat_journal nat_j;
		struct sit_journal sit_j;
		struct f2fs_extra_info info;
	};
} __attribute__((packed));

/*
 * Block-sized summary block structure
 * Layout of f2fs_summary block is
 * struct f2fs_summary entries[ENTRIES_IN_SUM];
 * struct f2fs_journal journal;
 * struct summary_footer footer;
 *
 * Do NOT use sizeof, use F2FS_BLKSIZE
 *
 */
struct f2fs_summary_block {
	struct f2fs_summary entries[0];
};
#define F2FS_SUMMARY_BLOCK_JOURNAL(blk) ((struct f2fs_journal *)(&(blk)->entries[ENTRIES_IN_SUM]))
#define F2FS_SUMMARY_BLOCK_FOOTER(blk) ((struct summary_footer *)&((char *)\
					(&(blk)->entries[0]))[F2FS_BLKSIZE - SUM_FOOTER_SIZE])

/*
 * For directory operations
 */
#define F2FS_DOT_HASH		0
#define F2FS_DDOT_HASH		F2FS_DOT_HASH
#define F2FS_MAX_HASH		(~((0x3ULL) << 62))
#define F2FS_HASH_COL_BIT	((0x1ULL) << 63)

typedef __le32	f2fs_hash_t;

/* One directory entry slot covers 8bytes-long file name */
#define F2FS_SLOT_LEN		8
#define F2FS_SLOT_LEN_BITS	3

#define GET_DENTRY_SLOTS(x)	((x + F2FS_SLOT_LEN - 1) >> F2FS_SLOT_LEN_BITS)

/* the number of dentry in a block */
#define NR_DENTRY_IN_BLOCK  ((BITS_PER_BYTE * F2FS_BLKSIZE) / \
				((SIZE_OF_DIR_ENTRY + F2FS_SLOT_LEN) * BITS_PER_BYTE + 1))
/* MAX level for dir lookup */
#define MAX_DIR_HASH_DEPTH	63

/* MAX buckets in one level of dir */
#define MAX_DIR_BUCKETS		(1 << ((MAX_DIR_HASH_DEPTH / 2) - 1))

#define SIZE_OF_DIR_ENTRY	11	/* by byte */
#define SIZE_OF_DENTRY_BITMAP	((NR_DENTRY_IN_BLOCK + BITS_PER_BYTE - 1) / \
					BITS_PER_BYTE)
#define SIZE_OF_RESERVED	(F2FS_BLKSIZE - ((SIZE_OF_DIR_ENTRY + \
				F2FS_SLOT_LEN) * \
				NR_DENTRY_IN_BLOCK + SIZE_OF_DENTRY_BITMAP))
#define MIN_INLINE_DENTRY_SIZE		40	/* just include '.' and '..' entries */

/* One directory entry slot representing F2FS_SLOT_LEN-sized file name */
struct f2fs_dir_entry {
	__le32 hash_code;	/* hash code of file name */
	__le32 ino;		/* inode number */
	__le16 name_len;	/* lengh of file name */
	__u8 file_type;		/* file type */
} __attribute__((packed));

static_assert(sizeof(struct f2fs_dir_entry) == 11, "");

/*
 * Block-sized directory entry block
 * On disk structure:
 * struct f2fs_dentry_block;
 * __u8 reserved[SIZE_OF_RESERVED];
 * struct f2fs_dir_entry dentry[NR_DENTRY_IN_BLOCK];
 * __u8 filename[NR_DENTRY_IN_BLOCK][F2FS_SLOT_LEN];
 *
 * Do NOT use sizeof. Use F2FS_BLKSIZE instead
 */
struct f2fs_dentry_block {
	/* validity bitmap for directory entries in each block */
	__u8 dentry_bitmap[0]; /* size is SIZE_OF_DENTRY_BITMAP, based on block size */
};

#define F2FS_DENTRY_BLOCK_DENTRIES(blk) ((struct f2fs_dir_entry *)\
			&((blk)->dentry_bitmap[SIZE_OF_DENTRY_BITMAP + SIZE_OF_RESERVED]))
#define F2FS_DENTRY_BLOCK_DENTRY(blk, i) (F2FS_DENTRY_BLOCK_DENTRIES((blk))[(i)])

#define F2FS_DENTRY_BLOCK_FILENAMES(blk) ((__u8(*)[F2FS_SLOT_LEN])&F2FS_DENTRY_BLOCK_DENTRY(blk,\
							NR_DENTRY_IN_BLOCK))
#define F2FS_DENTRY_BLOCK_FILENAME(blk, i) (&((__u8 *)&F2FS_DENTRY_BLOCK_DENTRY(blk,\
							NR_DENTRY_IN_BLOCK))[(i) * F2FS_SLOT_LEN])

/* for inline stuff */
#define DEF_INLINE_RESERVED_SIZE	1

/* for inline dir */
#define NR_INLINE_DENTRY(node)	(MAX_INLINE_DATA(node) * BITS_PER_BYTE / \
				((SIZE_OF_DIR_ENTRY + F2FS_SLOT_LEN) * \
				BITS_PER_BYTE + 1))
#define INLINE_DENTRY_BITMAP_SIZE(node)	((NR_INLINE_DENTRY(node) + \
					BITS_PER_BYTE - 1) / BITS_PER_BYTE)
#define INLINE_RESERVED_SIZE(node)	(MAX_INLINE_DATA(node) - \
				((SIZE_OF_DIR_ENTRY + F2FS_SLOT_LEN) * \
				NR_INLINE_DENTRY(node) + \
				INLINE_DENTRY_BITMAP_SIZE(node)))

/* file types used in inode_info->flags */
enum FILE_TYPE {
	F2FS_FT_UNKNOWN,
	F2FS_FT_REG_FILE,
	F2FS_FT_DIR,
	F2FS_FT_CHRDEV,
	F2FS_FT_BLKDEV,
	F2FS_FT_FIFO,
	F2FS_FT_SOCK,
	F2FS_FT_SYMLINK,
	F2FS_FT_MAX,
	/* added for fsck */
	F2FS_FT_ORPHAN,
	F2FS_FT_XATTR,
	F2FS_FT_LAST_FILE_TYPE = F2FS_FT_XATTR,
};

enum dot_type {
	NON_DOT,
	TYPE_DOT,
	TYPE_DOTDOT
};

#define LINUX_S_IFMT  00170000
#define LINUX_S_IFREG  0100000
#define LINUX_S_IFDIR  0040000
#define LINUX_S_ISREG(m)	(((m) & LINUX_S_IFMT) == LINUX_S_IFREG)
#define LINUX_S_ISDIR(m)	(((m) & LINUX_S_IFMT) == LINUX_S_IFDIR)

/* from f2fs/segment.h */
enum {
	LFS = 0,
	SSR
};

/* invalid sb types */
#define SB_FORCE_STOP		0x1	/* s_stop_reason is set */
#define SB_ABNORMAL_STOP	0x2	/* s_stop_reason is set except shutdown */
#define SB_FS_ERRORS		0x4	/* s_erros is set */
#define SB_INVALID		0x8	/* sb is invalid */
#define SB_ENCODE_FLAG		0x16	/* encode_flag */
#define SB_NEED_FIX		(SB_ABNORMAL_STOP | SB_FS_ERRORS |	\
				SB_INVALID | SB_ENCODE_FLAG)

#define MAX_CACHE_SUMS			8

/* feature list in Android */
enum {
	F2FS_FEATURE_NAT_BITS = 0x0001,
};

/* nolinear lookup tune */
enum {
	LINEAR_LOOKUP_DEFAULT = 0,
	LINEAR_LOOKUP_ENABLE,
	LINEAR_LOOKUP_DISABLE,
};

/* Fault inject control */
enum {
	FAULT_SEG_TYPE,
	FAULT_SUM_TYPE,
	FAULT_SUM_ENT,
	FAULT_NAT,
	FAULT_NODE,
	FAULT_XATTR_ENT,
	FAULT_COMPR,
	FAULT_INODE,
	FAULT_DENTRY,
	FAULT_DATA,
	FAULT_QUOTA,
	FAULT_MAX
};

#define F2FS_ALL_FAULT_TYPE	((1UL << (FAULT_MAX)) - 1)

struct f2fs_fault_info {
	int inject_ops;
	int inject_rate;
	unsigned int inject_type;
	unsigned int fault_cnt[FAULT_MAX];
};

extern const char *f2fs_fault_name[FAULT_MAX];
#define IS_FAULT_SET(fi, type) ((fi)->inject_type & (1UL << (type)))

struct f2fs_configuration {
	uint32_t conf_reserved_sections;
	uint32_t reserved_segments;
	uint32_t new_reserved_segments;
	int sparse_mode;
	int zoned_mode;
	int zoned_model;
	size_t zone_blocks;
	double overprovision;
	double new_overprovision;
	uint32_t cur_seg[NR_CURSEG_TYPE];
	uint32_t segs_per_sec;
	uint32_t secs_per_zone;
	uint32_t segs_per_zone;
	uint32_t start_sector;
	uint32_t total_segments;
	uint32_t sector_size;
	uint64_t device_size;
	uint64_t total_sectors;
	uint64_t wanted_total_sectors;
	uint64_t wanted_sector_size;
	uint64_t target_sectors;
	uint32_t sectors_per_blk;
	uint32_t blks_per_seg;
	__u8 init_version[VERSION_LEN + 1];
	__u8 sb_version[VERSION_LEN + 1];
	__u8 version[VERSION_LEN + 1];
	char *vol_label;
	char *vol_uuid;
	uint16_t s_encoding;
	uint16_t s_encoding_flags;
	int32_t kd;
	int32_t dump_fd;
	char *dump_symlink;
	int dump_sym_target_len;
	struct device_info devices[MAX_DEVICES];
	int ndevs;
	char *extension_list[2];
	const char *rootdev_name;
	int dbg_lv;
	int show_dentry;
	int trim;
	int trimmed;
	int func;
	void *private;
	int dry_run;
	int no_kernel_check;
	int fix_on;
	int force;
	int ignore_error;
	int defset;
	int bug_on;
	unsigned int invalid_sb;
	int bug_nat_bits;
	bool quota_fixed;
	int alloc_failed;
	int auto_fix;
	int layout;
	int show_file_map;
	u64 show_file_map_max_offset;
	int quota_fix;
	int preen_mode;
	int ro;
	int preserve_limits;		/* preserve quota limits */
	int large_nat_bitmap;
	int fix_chksum;			/* fix old cp.chksum position */
	int nolinear_lookup;		/* disable linear lookup */
	unsigned int feature;			/* defined features */
	unsigned int disabled_feature;	/* disabled feature, used for Android only */
	unsigned int quota_bits;	/* quota bits */
	time_t fixed_time;
	int roll_forward;
	bool need_fsync;
	bool need_whint;
	int whint;
	int aliased_devices;
	uint32_t aliased_segments;

	/* mkfs parameters */
	int fake_seed;
	uint32_t next_free_nid;
	uint32_t lpf_ino;
	uint32_t first_alias_ino;
	uint32_t root_uid;
	uint32_t root_gid;
	uint32_t blksize;
	uint32_t blksize_bits;

	/* defragmentation parameters */
	int defrag_shrink;
	uint64_t defrag_start;
	uint64_t defrag_len;
	uint64_t defrag_target;

	/* sload parameters */
	char *from_dir;
	char *mount_point;
	char *target_out_dir;
	char *fs_config_file;
#ifdef HAVE_LIBSELINUX
	struct selinux_opt seopt_file[8];
	int nr_opt;
#endif

	/* dump parameters */
	int preserve_perms;
	int preserve_symlinks;

	/* resize parameters */
	int safe_resize;

	/* precomputed fs UUID checksum for seeding other checksums */
	uint32_t chksum_seed;

	/* cache parameters */
	dev_cache_config_t cache_config;

	/* compression support for sload.f2fs */
	compress_config_t compress;

	block_t curseg_offset[NR_CURSEG_TYPE];
	struct f2fs_summary sum[NR_CURSEG_TYPE][MAX_CACHE_SUMS];
	union {
		struct f2fs_journal sit_jnl;
		char sit_bytes[F2FS_MAX_BLKSIZE];
	};
	union {
		struct f2fs_journal nat_jnl;
		char nat_bytes[F2FS_MAX_BLKSIZE];
	};

	/* Fault injection control */
	struct f2fs_fault_info fault_info;
};

extern int utf8_to_utf16(char *, const char *, size_t, size_t);
extern int utf16_to_utf8(char *, const char *, size_t, size_t);
extern int log_base_2(uint32_t);
extern unsigned int addrs_per_page(struct f2fs_inode *, bool);
extern u64 f2fs_max_file_offset(struct f2fs_inode *);
extern __u32 f2fs_inode_chksum(struct f2fs_node *);
extern __u32 f2fs_checkpoint_chksum(struct f2fs_checkpoint *);
extern int write_inode(struct f2fs_node *, u64, enum rw_hint);

extern int get_bits_in_byte(unsigned char n);
extern int test_and_set_bit_le(u32, u8 *);
extern int test_and_clear_bit_le(u32, u8 *);
extern int test_bit_le(u32, const u8 *);
extern int f2fs_test_bit(unsigned int, const char *);
extern int f2fs_set_bit(unsigned int, char *);
extern int f2fs_clear_bit(unsigned int, char *);
extern u64 find_next_bit_le(const u8 *, u64, u64);
extern u64 find_next_zero_bit_le(const u8 *, u64, u64);

extern uint32_t f2fs_cal_crc32(uint32_t, void *, int);
extern int f2fs_crc_valid(uint32_t blk_crc, void *buf, int len);

extern void f2fs_init_configuration(void);
extern int f2fs_devs_are_umounted(void);
extern int f2fs_dev_is_writable(void);
extern int f2fs_dev_is_umounted(char *);
extern int f2fs_get_device_info(void);
extern int f2fs_get_f2fs_info(void);
extern unsigned int calc_extra_isize(void);
extern int get_device_info(int);
extern int f2fs_init_sparse_file(void);
extern void f2fs_release_sparse_resource(void);
extern int f2fs_finalize_device(void);
extern int f2fs_fsync_device(void);

extern void dcache_init(void);
extern void dcache_release(void);

extern int dev_read(void *, __u64, size_t);
#ifdef POSIX_FADV_WILLNEED
extern int dev_readahead(__u64, size_t);
#else
extern int dev_readahead(__u64, size_t UNUSED(len));
#endif
extern enum rw_hint f2fs_io_type_to_rw_hint(int);
extern int dev_write(void *, __u64, size_t, enum rw_hint);
extern int dev_write_block(void *, __u64, enum rw_hint);
extern int dev_write_dump(void *, __u64, size_t);
#if !defined(__MINGW32__)
extern int dev_write_symlink(char *, size_t);
#endif
/* All bytes in the buffer must be 0 use dev_fill(). */
extern int dev_fill(void *, __u64, size_t, enum rw_hint);
extern int dev_fill_block(void *, __u64, enum rw_hint);

extern int dev_read_block(void *, __u64);
extern int dev_reada_block(__u64);

extern int dev_read_version(void *, __u64, size_t);
extern void get_kernel_version(__u8 *);
extern void get_kernel_uname_version(__u8 *);
f2fs_hash_t f2fs_dentry_hash(int, int, const unsigned char *, int);

static inline bool f2fs_has_extra_isize(struct f2fs_inode *inode)
{
	return (inode->i_inline & F2FS_EXTRA_ATTR);
}

static inline int __get_extra_isize(struct f2fs_inode *inode)
{
	if (f2fs_has_extra_isize(inode))
		return le16_to_cpu(inode->i_extra_isize) / sizeof(__le32);
	return 0;
}

extern struct f2fs_configuration c;
static inline int get_inline_xattr_addrs(struct f2fs_inode *inode)
{
	if (c.feature & F2FS_FEATURE_FLEXIBLE_INLINE_XATTR)
		return le16_to_cpu(inode->i_inline_xattr_size);
	else if (inode->i_inline & F2FS_INLINE_XATTR ||
			inode->i_inline & F2FS_INLINE_DENTRY)
		return DEFAULT_INLINE_XATTR_ADDRS;
	else
		return 0;
}

#define get_extra_isize(node)	__get_extra_isize(&node->i)

#define F2FS_ZONED_NONE		0
#define F2FS_ZONED_HA		1
#define F2FS_ZONED_HM		2

#ifdef HAVE_LINUX_BLKZONED_H

/* Let's just use v2, since v1 should be compatible with v2 */
#define BLK_ZONE_REP_CAPACITY   (1 << 0)
struct blk_zone_v2 {
	__u64   start;          /* Zone start sector */
	__u64   len;            /* Zone length in number of sectors */
	__u64   wp;             /* Zone write pointer position */
	__u8    type;           /* Zone type */
	__u8    cond;           /* Zone condition */
	__u8    non_seq;        /* Non-sequential write resources active */
	__u8    reset;          /* Reset write pointer recommended */
	__u8    resv[4];
	__u64   capacity;       /* Zone capacity in number of sectors */
	__u8    reserved[24];
};
#define blk_zone blk_zone_v2

struct blk_zone_report_v2 {
	__u64   sector;
	__u32   nr_zones;
	__u32   flags;
	struct blk_zone zones[0];
};
#define blk_zone_report blk_zone_report_v2

#define blk_zone_type(z)        (z)->type
#define blk_zone_conv(z)	((z)->type == BLK_ZONE_TYPE_CONVENTIONAL)
#define blk_zone_seq_req(z)	((z)->type == BLK_ZONE_TYPE_SEQWRITE_REQ)
#define blk_zone_seq_pref(z)	((z)->type == BLK_ZONE_TYPE_SEQWRITE_PREF)
#define blk_zone_seq(z)		(blk_zone_seq_req(z) || blk_zone_seq_pref(z))

static inline const char *
blk_zone_type_str(struct blk_zone *blkz)
{
	switch (blk_zone_type(blkz)) {
	case BLK_ZONE_TYPE_CONVENTIONAL:
		return( "Conventional" );
	case BLK_ZONE_TYPE_SEQWRITE_REQ:
		return( "Sequential-write-required" );
	case BLK_ZONE_TYPE_SEQWRITE_PREF:
		return( "Sequential-write-preferred" );
	}
	return( "Unknown-type" );
}

#define blk_zone_cond(z)	(z)->cond

static inline const char *
blk_zone_cond_str(struct blk_zone *blkz)
{
	switch (blk_zone_cond(blkz)) {
	case BLK_ZONE_COND_NOT_WP:
		return "Not-write-pointer";
	case BLK_ZONE_COND_EMPTY:
		return "Empty";
	case BLK_ZONE_COND_IMP_OPEN:
		return "Implicit-open";
	case BLK_ZONE_COND_EXP_OPEN:
		return "Explicit-open";
	case BLK_ZONE_COND_CLOSED:
		return "Closed";
	case BLK_ZONE_COND_READONLY:
		return "Read-only";
	case BLK_ZONE_COND_FULL:
		return "Full";
	case BLK_ZONE_COND_OFFLINE:
		return "Offline";
	}
	return "Unknown-cond";
}

/*
 * Handle kernel zone capacity support
 */
#define blk_zone_empty(z)	(blk_zone_cond(z) == BLK_ZONE_COND_EMPTY)
#define blk_zone_open(z)	(blk_zone_cond(z) == BLK_ZONE_COND_IMP_OPEN ||	\
				 blk_zone_cond(z) == BLK_ZONE_COND_EXP_OPEN)
#define blk_zone_sector(z)	(z)->start
#define blk_zone_length(z)	(z)->len
#define blk_zone_wp_sector(z)	(z)->wp
#define blk_zone_need_reset(z)	(int)(z)->reset
#define blk_zone_non_seq(z)	(int)(z)->non_seq
#define blk_zone_capacity(z, f) ((f & BLK_ZONE_REP_CAPACITY) ? \
					(z)->capacity : (z)->len)

#endif

struct blk_zone;

extern int f2fs_get_zoned_model(int);
extern int f2fs_get_zone_blocks(int);
extern int f2fs_report_zone(int, uint64_t, struct blk_zone *);
typedef int (report_zones_cb_t)(int i, void *, void *);
extern int f2fs_report_zones(int, report_zones_cb_t *, void *);
extern int f2fs_check_zones(int);
int f2fs_reset_zone(int, void *);
extern int f2fs_reset_zones(int);
int f2fs_finish_zone(int i, void *blkzone);
extern uint32_t f2fs_get_usable_segments(struct f2fs_super_block *sb);

#define SIZE_ALIGN(val, size)	(((val) + (size) - 1) / (size))
#define SEG_ALIGN(blks)		SIZE_ALIGN(blks, c.blks_per_seg)
#define ZONE_ALIGN(blks)	SIZE_ALIGN(blks, c.blks_per_seg * \
					c.segs_per_zone)

static inline uint32_t get_reserved(struct f2fs_super_block *sb, double ovp)
{
	uint32_t usable_main_segs = f2fs_get_usable_segments(sb);
	uint32_t segs_per_sec = round_up(usable_main_segs, get_sb(section_count));
	uint32_t reserved;

	if (c.conf_reserved_sections)
		reserved = c.conf_reserved_sections * segs_per_sec;
	else
		reserved = (100 / ovp + 1 + NR_CURSEG_TYPE) * segs_per_sec;

	/* Let's keep the section alignment */
	return round_up(reserved, segs_per_sec) * segs_per_sec;
}

static inline uint32_t overprovision_segment_buffer(struct f2fs_super_block *sb)
{
	/* Give 6 current sections to avoid huge GC overheads. */
	return 6 * get_sb(segs_per_sec);
}

static inline double get_best_overprovision(struct f2fs_super_block *sb)
{
	double ovp, candidate, end, diff, space;
	double max_ovp = 0, max_space = 0;
	uint32_t usable_main_segs = f2fs_get_usable_segments(sb);
	uint32_t reserved;

	if (get_sb(segment_count_main) < 256) {
		candidate = 10;
		end = 95;
		diff = 5;
	} else {
		candidate = 0.01;
		end = 10;
		diff = 0.01;
	}

	for (; candidate <= end; candidate += diff) {
		reserved = get_reserved(sb, candidate);
		ovp = (usable_main_segs - reserved) * candidate / 100;
		if (ovp < 0)
			continue;
		space = usable_main_segs - max((double)reserved, ovp) -
					overprovision_segment_buffer(sb);
		if (max_space < space) {
			max_space = space;
			max_ovp = candidate;
		}
	}
	return max_ovp;
}

static inline __le64 get_cp_crc(struct f2fs_checkpoint *cp)
{
	uint64_t cp_ver = get_cp(checkpoint_ver);
	size_t crc_offset = get_cp(checksum_offset);
	uint32_t crc = le32_to_cpu(*(__le32 *)((unsigned char *)cp +
							crc_offset));

	cp_ver |= ((uint64_t)crc << 32);
	return cpu_to_le64(cp_ver);
}

static inline int exist_qf_ino(struct f2fs_super_block *sb)
{
	int i;

	for (i = 0; i < F2FS_MAX_QUOTAS; i++)
		if (sb->qf_ino[i])
			return 1;
	return 0;
}

static inline int is_qf_ino(struct f2fs_super_block *sb, nid_t ino)
{
	int i;

	for (i = 0; i < F2FS_MAX_QUOTAS; i++)
		if (sb->qf_ino[i] == ino)
			return 1;
	return 0;
}

static inline void show_version(const char *prog)
{
#if defined(F2FS_TOOLS_VERSION) && defined(F2FS_TOOLS_DATE)
	MSG(0, "%s %s (%s)\n", prog, F2FS_TOOLS_VERSION, F2FS_TOOLS_DATE);
#else
	MSG(0, "%s -- version not supported\n", prog);
#endif
}

static inline void f2fs_init_inode(struct f2fs_super_block *sb,
		struct f2fs_node *raw_node, nid_t ino, time_t mtime, mode_t mode)
{
	F2FS_NODE_FOOTER(raw_node)->nid = cpu_to_le32(ino);
	F2FS_NODE_FOOTER(raw_node)->ino = cpu_to_le32(ino);
	F2FS_NODE_FOOTER(raw_node)->cp_ver = cpu_to_le64(1);

	raw_node->i.i_uid = cpu_to_le32(c.root_uid);
	raw_node->i.i_gid = cpu_to_le32(c.root_gid);

	raw_node->i.i_atime = cpu_to_le32(mtime);
	raw_node->i.i_atime_nsec = 0;
	raw_node->i.i_ctime = cpu_to_le32(mtime);
	raw_node->i.i_ctime_nsec = 0;
	raw_node->i.i_mtime = cpu_to_le32(mtime);
	raw_node->i.i_mtime_nsec = 0;
	raw_node->i.i_generation = 0;
	raw_node->i.i_xattr_nid = 0;
	raw_node->i.i_flags = 0;
	raw_node->i.i_current_depth = cpu_to_le32(LINUX_S_ISDIR(mode) ? 1 : 0);
	raw_node->i.i_dir_level = DEF_DIR_LEVEL;
	raw_node->i.i_mode = cpu_to_le16(mode);
	raw_node->i.i_links = cpu_to_le32(LINUX_S_ISDIR(mode) ? 2 : 1);

	/* for dentry block in directory */
	raw_node->i.i_size = cpu_to_le64(1 << get_sb(log_blocksize));
	raw_node->i.i_blocks = cpu_to_le64(2);

	if (c.feature & F2FS_FEATURE_EXTRA_ATTR) {
		raw_node->i.i_inline = F2FS_EXTRA_ATTR;
		raw_node->i.i_extra_isize = cpu_to_le16(calc_extra_isize());
	}

	if (c.feature & F2FS_FEATURE_PRJQUOTA)
		raw_node->i.i_projid = cpu_to_le32(F2FS_DEF_PROJID);

	if (c.feature & F2FS_FEATURE_INODE_CRTIME) {
		raw_node->i.i_crtime = cpu_to_le32(mtime);
		raw_node->i.i_crtime_nsec = 0;
	}

	if (c.feature & F2FS_FEATURE_COMPRESSION) {
		raw_node->i.i_compr_blocks = 0;
		raw_node->i.i_compress_algorithm = 0;
		raw_node->i.i_log_cluster_size = 0;
		raw_node->i.i_compress_flag = 0;
	}

	raw_node->i.i_ext.fofs = 0;
	raw_node->i.i_ext.blk_addr = 0;
	raw_node->i.i_ext.len = 0;
}

struct feature {
	char *name;
	u32 mask;
	u32 settable;
};

#define INIT_FEATURE_TABLE						\
struct feature feature_table[] = {					\
	{ "encrypt",			F2FS_FEATURE_ENCRYPT,		1}, \
	{ "blkzoned",			F2FS_FEATURE_BLKZONED,		0}, \
	{ "extra_attr",			F2FS_FEATURE_EXTRA_ATTR,	1}, \
	{ "project_quota",		F2FS_FEATURE_PRJQUOTA,		1}, \
	{ "inode_checksum",		F2FS_FEATURE_INODE_CHKSUM,	1}, \
	{ "flexible_inline_xattr",	F2FS_FEATURE_FLEXIBLE_INLINE_XATTR,1}, \
	{ "quota",			F2FS_FEATURE_QUOTA_INO,		1}, \
	{ "inode_crtime",		F2FS_FEATURE_INODE_CRTIME,	1}, \
	{ "lost_found",			F2FS_FEATURE_LOST_FOUND,	1}, \
	{ "verity",			F2FS_FEATURE_VERITY,		1}, \
	{ "sb_checksum",		F2FS_FEATURE_SB_CHKSUM,		1}, \
	{ "casefold",			F2FS_FEATURE_CASEFOLD,		1}, \
	{ "compression",		F2FS_FEATURE_COMPRESSION,	1}, \
	{ "ro",				F2FS_FEATURE_RO,		1}, \
	{ NULL,				0x0,				0}, \
};

static inline u32 feature_map(struct feature *table, char *feature)
{
	struct feature *p;
	for (p = table; p->name; p++) {
		if (!p->settable)
			continue;
		if (strcmp(p->name, feature))
			continue;
		break;
	}
	return p->mask;
}

static inline char *feature_name(struct feature *table, u32 mask)
{
	struct feature *p;
	for (p = table; p->name; p++) {
		if (p->mask != mask)
			continue;
		break;
	}
	return p->name;
}

static inline int set_feature_bits(struct feature *table, char *features)
{
	u32 mask = feature_map(table, features);
	if (mask) {
		c.feature |= mask;
	} else {
		MSG(0, "Error: Wrong features %s\n", features);
		return -1;
	}
	return 0;
}

static inline int parse_feature(struct feature *table, const char *features)
{
	char *buf, *sub, *next;

	buf = strdup(features);
	if (!buf)
		return -1;

	for (sub = buf; sub && *sub; sub = next ? next + 1 : NULL) {
		/* Skip the beginning blanks */
		while (*sub && *sub == ' ')
			sub++;
		next = sub;
		/* Skip a feature word */
		while (*next && *next != ' ' && *next != ',')
			next++;

		if (*next == 0)
			next = NULL;
		else
			*next = 0;

		if (set_feature_bits(table, sub)) {
			free(buf);
			return -1;
		}
	}
	free(buf);
	return 0;
}

static inline int parse_root_owner(char *ids,
			uint32_t *root_uid, uint32_t *root_gid)
{
	char *uid = ids;
	char *gid = NULL;
	int i;

	/* uid:gid */
	for (i = 0; i < strlen(ids) - 1; i++)
		if (*(ids + i) == ':')
			gid = ids + i + 1;
	if (!gid)
		return -1;

	*root_uid = atoi(uid);
	*root_gid = atoi(gid);
	return 0;
}

/*
 * NLS definitions
 */
struct f2fs_nls_table {
	int version;
	const struct f2fs_nls_ops *ops;
};

struct f2fs_nls_ops {
	int (*casefold)(const struct f2fs_nls_table *charset,
			const unsigned char *str, size_t len,
			unsigned char *dest, size_t dlen);
};

extern const struct f2fs_nls_table *f2fs_load_nls_table(int encoding);
#define F2FS_ENC_UTF8_12_0	1

extern int f2fs_str2encoding(const char *string);
extern char *f2fs_encoding2str(const int encoding);
extern int f2fs_get_encoding_flags(int encoding);
extern int f2fs_str2encoding_flags(char **param, __u16 *flags);

static inline void check_block_struct_sizes(void)
{
	/* Check Orphan Block Size */
	assert(F2FS_ORPHANS_PER_BLOCK * sizeof(__le32)
			+ sizeof(struct orphan_block_footer) == F2FS_BLKSIZE);

	/* Check Inode Block Size */
	assert(offsetof(struct f2fs_inode, i_extra_isize) + DEF_ADDRS_PER_INODE * sizeof(__le32)
		+ sizeof(struct f2fs_inode_nids) + sizeof(struct node_footer) == F2FS_BLKSIZE);

	/* Check Direct Block Size */
	assert(DEF_ADDRS_PER_BLOCK * sizeof(__le32) + sizeof(struct node_footer) == F2FS_BLKSIZE);

	/* Check Indirect Block Size */
	assert(NIDS_PER_BLOCK * sizeof(__le32) + sizeof(struct node_footer) == F2FS_BLKSIZE);

	/* Check NAT Block Size */
	assert((NAT_ENTRY_PER_BLOCK + 1) * sizeof(struct f2fs_nat_entry) > F2FS_BLKSIZE);
	assert(NAT_ENTRY_PER_BLOCK * sizeof(struct f2fs_nat_entry) <= F2FS_BLKSIZE);

	/* Check SIT Block Size */
	assert((SIT_ENTRY_PER_BLOCK + 1) * sizeof(struct f2fs_sit_entry) > F2FS_BLKSIZE);
	assert(SIT_ENTRY_PER_BLOCK * sizeof(struct f2fs_sit_entry) <= F2FS_BLKSIZE);

	/* Check NAT Journal Block Size */
	assert(sizeof(struct f2fs_summary) * ENTRIES_IN_SUM
			+ offsetof(struct f2fs_journal, nat_j)
			+ NAT_JOURNAL_ENTRIES * sizeof(struct nat_journal_entry)
			+ NAT_JOURNAL_RESERVED + sizeof(struct summary_footer) == F2FS_BLKSIZE);

	/* Check SIT Journal Block Size */
	assert(sizeof(struct f2fs_summary) * ENTRIES_IN_SUM
			+ offsetof(struct f2fs_journal, sit_j)
			+ SIT_JOURNAL_ENTRIES * sizeof(struct sit_journal_entry)
			+ SIT_JOURNAL_RESERVED + sizeof(struct summary_footer) == F2FS_BLKSIZE);

	/* Check Info Journal Block Size */
	assert(sizeof(struct f2fs_summary) * ENTRIES_IN_SUM + sizeof(__le64)
			+ offsetof(struct f2fs_journal, info)
			+ EXTRA_INFO_RESERVED + sizeof(struct summary_footer) == F2FS_BLKSIZE);

	/* Check Dentry Block Size */
	assert(sizeof(__u8) * (SIZE_OF_DENTRY_BITMAP + SIZE_OF_RESERVED)
			+ NR_DENTRY_IN_BLOCK * sizeof(struct f2fs_dir_entry)
			+ NR_DENTRY_IN_BLOCK * F2FS_SLOT_LEN * sizeof(u8) == F2FS_BLKSIZE);
}

/* Fault inject control */
#define time_to_inject(type) __time_to_inject(type, __func__, \
					__builtin_return_address(0))
static inline bool __time_to_inject(int type, const char *func,
		const char *parent_func)
{
	struct f2fs_fault_info *ffi = &c.fault_info;

	if (!ffi->inject_rate)
		return false;

	if (!IS_FAULT_SET(ffi, type))
		return false;

	ffi->inject_ops++;
	if (ffi->inject_ops >= ffi->inject_rate) {
		ffi->inject_ops = 0;
		if (ffi->fault_cnt[type] != UINT_MAX)
			ffi->fault_cnt[type]++;
		MSG(0, "inject %s in %s of %p\n",
				f2fs_fault_name[type], func, parent_func);
		return true;
	}
	return false;
}

#endif	/*__F2FS_FS_H */
