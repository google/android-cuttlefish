/*
 * ioctl.h - f2fs ioctl header
 *
 * Authors: Jaegeuk Kim <jaegeuk@kernel.org>
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#ifdef HAVE_LINUX_TYPES_H
#include <linux/types.h>
#endif
#ifdef HAVE_LINUX_FIEMAP_H
#include <linux/fiemap.h>
#endif
#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#ifdef HAVE_LINUX_VERITY_H
#include <linux/fsverity.h>
#endif

#include <sys/types.h>

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

typedef uint64_t	u64;
typedef uint32_t	u32;
typedef uint16_t	u16;
typedef uint8_t		u8;

#ifndef HAVE_LINUX_TYPES_H
typedef u8	__u8;
typedef u16	__u16;
typedef u32	__u32;
typedef u16	__le16;
typedef u32	__le32;
typedef u16	__be16;
typedef u32	__be32;
#endif

#define F2FS_DEFAULT_BLKSIZE	4096
#define NEW_ADDR	0xFFFFFFFF

#ifndef FS_IOC_GETFLAGS
#define FS_IOC_GETFLAGS			_IOR('f', 1, long)
#endif
#ifndef FS_IOC_SETFLAGS
#define FS_IOC_SETFLAGS			_IOW('f', 2, long)
#endif

#define F2FS_IOCTL_MAGIC		0xf5
#define F2FS_IOC_GETFLAGS		FS_IOC_GETFLAGS
#define F2FS_IOC_SETFLAGS		FS_IOC_SETFLAGS

#define F2FS_IOC_START_ATOMIC_WRITE	_IO(F2FS_IOCTL_MAGIC, 1)
#define F2FS_IOC_COMMIT_ATOMIC_WRITE	_IO(F2FS_IOCTL_MAGIC, 2)
#define F2FS_IOC_START_VOLATILE_WRITE	_IO(F2FS_IOCTL_MAGIC, 3)
#define F2FS_IOC_RELEASE_VOLATILE_WRITE	_IO(F2FS_IOCTL_MAGIC, 4)
#define F2FS_IOC_ABORT_VOLATILE_WRITE	_IO(F2FS_IOCTL_MAGIC, 5)
#define F2FS_IOC_GARBAGE_COLLECT	_IOW(F2FS_IOCTL_MAGIC, 6, __u32)
#define F2FS_IOC_WRITE_CHECKPOINT	_IO(F2FS_IOCTL_MAGIC, 7)
#define F2FS_IOC_DEFRAGMENT		_IOWR(F2FS_IOCTL_MAGIC, 8,	\
						struct f2fs_defragment)
#define F2FS_IOC_MOVE_RANGE		_IOWR(F2FS_IOCTL_MAGIC, 9,	\
						struct f2fs_move_range)
#define F2FS_IOC_FLUSH_DEVICE		_IOW(F2FS_IOCTL_MAGIC, 10,	\
						struct f2fs_flush_device)
#define F2FS_IOC_GARBAGE_COLLECT_RANGE	_IOW(F2FS_IOCTL_MAGIC, 11,	\
						struct f2fs_gc_range)
#define F2FS_IOC_GET_FEATURES		_IOR(F2FS_IOCTL_MAGIC, 12, __u32)
#define F2FS_IOC_SET_PIN_FILE		_IOW(F2FS_IOCTL_MAGIC, 13, __u32)
#define F2FS_IOC_GET_PIN_FILE		_IOR(F2FS_IOCTL_MAGIC, 14, __u32)
#define F2FS_IOC_PRECACHE_EXTENTS	_IO(F2FS_IOCTL_MAGIC, 15)
#define F2FS_IOC_RESIZE_FS		_IOW(F2FS_IOCTL_MAGIC, 16, __u64)
#define F2FS_IOC_GET_COMPRESS_BLOCKS	_IOR(F2FS_IOCTL_MAGIC, 17, __u64)
#define F2FS_IOC_RELEASE_COMPRESS_BLOCKS				\
					_IOR(F2FS_IOCTL_MAGIC, 18, __u64)
#define F2FS_IOC_RESERVE_COMPRESS_BLOCKS				\
					_IOR(F2FS_IOCTL_MAGIC, 19, __u64)
#define F2FS_IOC_GET_COMPRESS_OPTION    _IOR(F2FS_IOCTL_MAGIC, 21,      \
						struct f2fs_comp_option)
#define F2FS_IOC_SET_COMPRESS_OPTION    _IOW(F2FS_IOCTL_MAGIC, 22,      \
						struct f2fs_comp_option)
#define F2FS_IOC_DECOMPRESS_FILE        _IO(F2FS_IOCTL_MAGIC, 23)
#define F2FS_IOC_COMPRESS_FILE          _IO(F2FS_IOCTL_MAGIC, 24)
#define F2FS_IOC_START_ATOMIC_REPLACE	_IO(F2FS_IOCTL_MAGIC, 25)
#define F2FS_IOC_GET_DEV_ALIAS_FILE	_IOR(F2FS_IOCTL_MAGIC, 26, __u32)
#define F2FS_IOC_IO_PRIO		_IOW(F2FS_IOCTL_MAGIC, 27, __u32)

#ifndef FSCRYPT_POLICY_V1
#define FSCRYPT_POLICY_V1		0
#define FSCRYPT_KEY_DESCRIPTOR_SIZE	8
struct fscrypt_policy_v1 {
	__u8 version;
	__u8 contents_encryption_mode;
	__u8 filenames_encryption_mode;
	__u8 flags;
	__u8 master_key_descriptor[FSCRYPT_KEY_DESCRIPTOR_SIZE];
};
#endif
#ifndef FS_IOC_GET_ENCRYPTION_POLICY
#define FS_IOC_GET_ENCRYPTION_POLICY		_IOW('f', 21, struct fscrypt_policy_v1)
#endif

#ifndef FSCRYPT_POLICY_V2
#define FSCRYPT_POLICY_V2		2
#define FSCRYPT_KEY_IDENTIFIER_SIZE	16
struct fscrypt_policy_v2 {
	__u8 version;
	__u8 contents_encryption_mode;
	__u8 filenames_encryption_mode;
	__u8 flags;
	__u8 __reserved[4];
	__u8 master_key_identifier[FSCRYPT_KEY_IDENTIFIER_SIZE];
};
/* Struct passed to FS_IOC_GET_ENCRYPTION_POLICY_EX */
struct fscrypt_get_policy_ex_arg {
	__u64 policy_size; /* input/output */
	union {
		__u8 version;
		struct fscrypt_policy_v1 v1;
		struct fscrypt_policy_v2 v2;
	} policy; /* output */
};
#endif
#ifndef FS_IOC_GET_ENCRYPTION_POLICY_EX
#define FS_IOC_GET_ENCRYPTION_POLICY_EX		_IOWR('f', 22, __u8[9]) /* size + version */
#endif

#define F2FS_IOC_SET_ENCRYPTION_POLICY	FS_IOC_SET_ENCRYPTION_POLICY
#define F2FS_IOC_GET_ENCRYPTION_POLICY	FS_IOC_GET_ENCRYPTION_POLICY
#define F2FS_IOC_GET_ENCRYPTION_PWSALT	FS_IOC_GET_ENCRYPTION_PWSALT

#ifndef FS_IOC_ENABLE_VERITY
#define FS_IOC_ENABLE_VERITY    _IOW('f', 133, struct fsverity_enable_arg)
#define FS_VERITY_HASH_ALG_SHA256       1
struct fsverity_enable_arg {
	__u32 version;
	__u32 hash_algorithm;
	__u32 block_size;
	__u32 salt_size;
	__u64 salt_ptr;
	__u32 sig_size;
	__u32 __reserved1;
	__u64 sig_ptr;
	__u64 __reserved2[11];
};
#endif
/*
 * Inode flags
 */
#define F2FS_NOCOW_FL			0x00800000 /* Do not cow file */

/*
 * should be same as XFS_IOC_GOINGDOWN.
 * Flags for going down operation used by FS_IOC_GOINGDOWN
 */
#define F2FS_IOC_SHUTDOWN	_IOR('X', 125, __u32)	/* Shutdown */
#define F2FS_GOING_DOWN_FULLSYNC	0x0	/* going down with full sync */
#define F2FS_GOING_DOWN_METASYNC	0x1	/* going down with metadata */
#define F2FS_GOING_DOWN_NOSYNC		0x2	/* going down */
#define F2FS_GOING_DOWN_METAFLUSH	0x3	/* going down with meta flush */
#define F2FS_GOING_DOWN_NEED_FSCK	0x4	/* going down to trigger fsck */
#define F2FS_GOING_DOWN_MAX		0x5

#if defined(__KERNEL__) && defined(CONFIG_COMPAT)
/*
 * ioctl commands in 32 bit emulation
 */
#define F2FS_IOC32_GETFLAGS		FS_IOC32_GETFLAGS
#define F2FS_IOC32_SETFLAGS		FS_IOC32_SETFLAGS
#define F2FS_IOC32_GETVERSION		FS_IOC32_GETVERSION
#endif

#define F2FS_IOC_FSGETXATTR		FS_IOC_FSGETXATTR
#define F2FS_IOC_FSSETXATTR		FS_IOC_FSSETXATTR

#define F2FS_SYSTEM_ADVISE_NAME	"system.advise"
#define FADVISE_COLD_BIT	0x01
#define FADVISE_LOST_PINO_BIT	0x02
#define FADVISE_ENCRYPT_BIT	0x04
#define FADVISE_ENC_NAME_BIT	0x08
#define FADVISE_KEEP_SIZE_BIT	0x10
#define FADVISE_HOT_BIT		0x20
#define FADVISE_VERITY_BIT	0x40
#define FADVISE_TRUNC_BIT	0x80

/* used for F2FS_IOC_IO_PRIO */
enum {
	F2FS_IOPRIO_WRITE = 1,  /* high write priority */
};

#ifndef FS_IMMUTABLE_FL
#define FS_IMMUTABLE_FL			0x00000010 /* Immutable file */
#endif

#ifndef FS_ENCRYPT_FL
#define FS_ENCRYPT_FL			0x00000800 /* Encrypted file */
#endif
#ifndef FS_VERITY_FL
#define FS_VERITY_FL			0x00100000 /* Verity protected inode */
#endif
#ifndef FS_INLINE_DATA_FL
#define FS_INLINE_DATA_FL		0x10000000 /* Inline data for regular/symlink files */
#endif
#ifndef FS_NOCOW_FL
#define FS_NOCOW_FL			0x00800000 /* Do not cow file */
#endif
#ifndef FS_NOCOMP_FL
#define FS_NOCOMP_FL			0x00000400 /* Don't compress */
#endif
#ifndef FS_COMPR_FL
#define FS_COMPR_FL			0x00000004 /* Compress file */
#endif
#ifndef FS_CASEFOLD_FL
#define FS_CASEFOLD_FL			0x40000000 /* Folder is case insensitive */
#endif

struct f2fs_gc_range {
	u32 sync;
	u64 start;
	u64 len;
};

struct f2fs_defragment {
	u64 start;
	u64 len;
};

struct f2fs_move_range {
	u32 dst_fd;		/* destination fd */
	u64 pos_in;		/* start position in src_fd */
	u64 pos_out;		/* start position in dst_fd */
	u64 len;		/* size to move */
};

struct f2fs_flush_device {
	u32 dev_num;		/* device number to flush */
	u32 segments;		/* # of segments to flush */
};

struct f2fs_comp_option {
	u8 algorithm;
	u8 log_cluster_size;
};
