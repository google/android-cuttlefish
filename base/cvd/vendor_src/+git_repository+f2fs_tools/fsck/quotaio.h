/** quotaio.h
 *
 * Interface to the quota library.
 *
 * The quota library provides interface for creating and updating the quota
 * files and the ext4 superblock fields. It supports the new VFS_V1 quota
 * format. The quota library also provides support for keeping track of quotas
 * in memory.
 *
 * Aditya Kali <adityakali@google.com>
 * Header of IO operations for quota utilities
 *
 * Jan Kara <jack@suse.cz>
 *
 * Hyojun Kim <hyojun@google.com> - Ported to f2fs-tools
 */

#ifndef GUARD_QUOTAIO_H
#define GUARD_QUOTAIO_H

#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "dict.h"
#include "f2fs_fs.h"
#include "f2fs.h"
#include "node.h"
#include "fsck.h"

#include "dqblk_v2.h"

typedef int64_t qsize_t;	/* Type in which we store size limitations */
typedef int32_t f2fs_ino_t;
typedef int errcode_t;

enum quota_type {
	USRQUOTA = 0,
	GRPQUOTA = 1,
	PRJQUOTA = 2,
	MAXQUOTAS = 3,
};

#if MAXQUOTAS > 32
#error "cannot have more than 32 quota types to fit in qtype_bits"
#endif

enum qf_szchk_type_t {
	QF_SZCHK_NONE,
	QF_SZCHK_ERR,
	QF_SZCHK_INLINE,
	QF_SZCHK_REGFILE,
};

extern int cur_qtype;
extern u32 qf_last_blkofs[];
extern enum qf_szchk_type_t qf_szchk_type[];
extern u64 qf_maxsize[];

#define QUOTA_USR_BIT (1 << USRQUOTA)
#define QUOTA_GRP_BIT (1 << GRPQUOTA)
#define QUOTA_PRJ_BIT (1 << PRJQUOTA)
#define QUOTA_ALL_BIT (QUOTA_USR_BIT | QUOTA_GRP_BIT | QUOTA_PRJ_BIT)

typedef struct quota_ctx *quota_ctx_t;

struct quota_ctx {
	struct f2fs_sb_info *sbi;
	struct dict_t *quota_dict[MAXQUOTAS];
	struct quota_handle *quota_file[MAXQUOTAS];
	struct dict_t linked_inode_dict;
};

/*
 * Definitions of magics and versions of current quota files
 */
#define INITQMAGICS {\
	0xd9c01f11,	/* USRQUOTA */\
	0xd9c01927,	/* GRPQUOTA */\
	0xd9c03f14	/* PRJQUOTA */\
}

/* Size of blocks in which are counted size limits in generic utility parts */
#define QUOTABLOCK_BITS 10
#define QUOTABLOCK_SIZE (1 << QUOTABLOCK_BITS)
#define toqb(x) (((x) + QUOTABLOCK_SIZE - 1) >> QUOTABLOCK_BITS)

/* Quota format type IDs */
#define	QFMT_VFS_OLD 1
#define	QFMT_VFS_V0 2
#define	QFMT_VFS_V1 4

/*
 * The following constants define the default amount of time given a user
 * before the soft limits are treated as hard limits (usually resulting
 * in an allocation failure). The timer is started when the user crosses
 * their soft limit, it is reset when they go below their soft limit.
 */
#define MAX_IQ_TIME  604800	/* (7*24*60*60) 1 week */
#define MAX_DQ_TIME  604800	/* (7*24*60*60) 1 week */

#define IOFL_INFODIRTY	0x01	/* Did info change? */

struct quotafile_ops;

/* Generic information about quotafile */
struct util_dqinfo {
	time_t dqi_bgrace;	/* Block grace time for given quotafile */
	time_t dqi_igrace;	/* Inode grace time for given quotafile */
	union {
		struct v2_mem_dqinfo v2_mdqi;
	} u;			/* Format specific info about quotafile */
};

struct quota_file {
	struct f2fs_sb_info *sbi;
	f2fs_ino_t ino;
	int64_t filesize;
};

/* Structure for one opened quota file */
struct quota_handle {
	enum quota_type qh_type;	/* Type of quotafile */
	int qh_fmt;		/* Quotafile format */
	int qh_file_flags;
	int qh_io_flags;	/* IO flags for file */
	struct quota_file qh_qf;
	unsigned int (*read)(struct quota_file *qf, long offset,
			 void *buf, unsigned int size);
	unsigned int (*write)(struct quota_file *qf, long offset,
			  void *buf, unsigned int size);
	struct quotafile_ops *qh_ops;	/* Operations on quotafile */
	struct util_dqinfo qh_info;	/* Generic quotafile info */
};

/* Utility quota block */
struct util_dqblk {
	qsize_t dqb_ihardlimit;
	qsize_t dqb_isoftlimit;
	qsize_t dqb_curinodes;
	qsize_t dqb_bhardlimit;
	qsize_t dqb_bsoftlimit;
	qsize_t dqb_curspace;
	time_t dqb_btime;
	time_t dqb_itime;
	union {
		struct v2_mem_dqblk v2_mdqb;
	} u;			/* Format specific dquot information */
};

/* Structure for one loaded quota */
struct dquot {
	struct dquot *dq_next;	/* Pointer to next dquot in the list */
	qid_t dq_id;		/* ID dquot belongs to */
	int dq_flags;		/* Some flags for utils */
	struct quota_handle *dq_h;	/* Handle of quotafile for this dquot */
	struct util_dqblk dq_dqb;	/* Parsed data of dquot */
};

#define DQF_SEEN	0x0001

/* Structure of quotafile operations */
struct quotafile_ops {
	/* Check whether quotafile is in our format */
	int (*check_file) (struct quota_handle *h, int type);
	/* Open quotafile */
	int (*init_io) (struct quota_handle *h, enum quota_type qtype);
	/* Create new quotafile */
	int (*new_io) (struct quota_handle *h);
	/* Write all changes and close quotafile */
	int (*end_io) (struct quota_handle *h);
	/* Write info about quotafile */
	int (*write_info) (struct quota_handle *h);
	/* Read dquot into memory */
	struct dquot *(*read_dquot) (struct quota_handle *h, qid_t id);
	/* Write given dquot to disk */
	int (*commit_dquot) (struct dquot *dquot);
	/* Scan quotafile and call callback on every structure */
	int (*scan_dquots) (struct quota_handle *h,
			    int (*process_dquot) (struct dquot *dquot,
						  void *data),
			    void *data);
	/* Function to print format specific file information */
	int (*report) (struct quota_handle *h, int verbose);
};

#ifdef __CHECKER__
# ifndef __bitwise
#  define __bitwise             __attribute__((bitwise))
# endif
#define __force                 __attribute__((force))
#else
# ifndef __bitwise
#  define __bitwise
# endif
#define __force
#endif

/* Open existing quotafile of given type (and verify its format) on given
 * filesystem. */
errcode_t quota_file_open(struct f2fs_sb_info *sbi, struct quota_handle *h,
			  enum quota_type qtype, int flags);

/* Create new quotafile of specified format on given filesystem */
errcode_t quota_file_create(struct f2fs_sb_info *sbi, struct quota_handle *h,
		enum quota_type qtype);

/* Close quotafile */
errcode_t quota_file_close(struct f2fs_sb_info *sbi, struct quota_handle *h,
		int update_filesize);

/* Get empty quota structure */
struct dquot *get_empty_dquot(void);
const char *quota_type2name(enum quota_type qtype);
void update_grace_times(struct dquot *q);

/* In mkquota.c */
errcode_t quota_init_context(struct f2fs_sb_info *sbi);
void quota_data_inodes(quota_ctx_t qctx, struct f2fs_inode *inode, int adjust);
void quota_data_add(quota_ctx_t qctx, struct f2fs_inode *inode, qsize_t space);
void quota_data_sub(quota_ctx_t qctx, struct f2fs_inode *inode, qsize_t space);
errcode_t quota_write_inode(struct f2fs_sb_info *sbi, enum quota_type qtype);
void quota_add_inode_usage(quota_ctx_t qctx, f2fs_ino_t ino,
		struct f2fs_inode* inode);
void quota_release_context(quota_ctx_t *qctx);
errcode_t quota_compare_and_update(struct f2fs_sb_info *sbi,
		enum quota_type qtype, int *usage_inconsistent,
		int preserve_limits);

static inline errcode_t quota_get_mem(unsigned long size, void *ptr)
{
        void *pp;

        pp = malloc(size);
        if (!pp)
                return -1;
        memcpy(ptr, &pp, sizeof (pp));
        return 0;
}

static inline errcode_t quota_get_memzero(unsigned long size, void *ptr)
{
        void *pp;

        pp = malloc(size);
        if (!pp)
                return -1;
        memset(pp, 0, size);
        memcpy(ptr, &pp, sizeof(pp));
        return 0;
}

static inline errcode_t quota_free_mem(void *ptr)
{
        void *p;

        memcpy(&p, ptr, sizeof(p));
        free(p);
        p = 0;
        memcpy(ptr, &p, sizeof(p));
        return 0;
}

#endif /* GUARD_QUOTAIO_H */
