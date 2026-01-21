/*
 *
 * Header file for disk format of new quotafile format
 *
 * Copied essential definitions and structures for mkfs.f2fs from quotaio.h
 *
 * Aditya Kali <adityakali@google.com>
 * Jan Kara <jack@suse.cz>
 * Hyojun Kim <hyojun@google.com> - Ported to f2fs-tools
 *
 */
#ifndef F2FS_QUOTA_H
#define F2FS_QUOTA_H

enum quota_type {
	USRQUOTA = 0,
	GRPQUOTA = 1,
	PRJQUOTA = 2,
	MAXQUOTAS = 3,
};

#if MAXQUOTAS > 32
#error "cannot have more than 32 quota types to fit in qtype_bits"
#endif

#define QUOTA_USR_BIT (1 << USRQUOTA)
#define QUOTA_GRP_BIT (1 << GRPQUOTA)
#define QUOTA_PRJ_BIT (1 << PRJQUOTA)
#define QUOTA_ALL_BIT (QUOTA_USR_BIT | QUOTA_GRP_BIT | QUOTA_PRJ_BIT)

/*
 * Definitions of magics and versions of current quota files
 */
#define INITQMAGICS {\
	0xd9c01f11,	/* USRQUOTA */\
	0xd9c01927,	/* GRPQUOTA */\
	0xd9c03f14      /* PRJQUOTA */\
}

#define V2_DQINFOOFF	sizeof(struct v2_disk_dqheader)	/* Offset of info header in file */

#define MAX_IQ_TIME  604800	/* (7*24*60*60) 1 week */
#define MAX_DQ_TIME  604800	/* (7*24*60*60) 1 week */

#define QT_TREEOFF	1	/* Offset of tree in file in blocks */

struct v2_disk_dqheader {
	uint32_t dqh_magic;	/* Magic number identifying file */
	uint32_t dqh_version;	/* File version */
};

static_assert(sizeof(struct v2_disk_dqheader) == 8, "");

/* Header with type and version specific information */
struct v2_disk_dqinfo {
	uint32_t dqi_bgrace;	/* Time before block soft limit becomes hard limit */
	uint32_t dqi_igrace;	/* Time before inode soft limit becomes hard limit */
	uint32_t dqi_flags;	/* Flags for quotafile (DQF_*) */
	uint32_t dqi_blocks;	/* Number of blocks in file */
	uint32_t dqi_free_blk;	/* Number of first free block in the list */
	uint32_t dqi_free_entry;	/* Number of block with at least one free entry */
};

static_assert(sizeof(struct v2_disk_dqinfo) == 24, "");

struct v2r1_disk_dqblk {
	__le32 dqb_id;  	/* id this quota applies to */
	__le32 dqb_pad;
	__le64 dqb_ihardlimit;  /* absolute limit on allocated inodes */
	__le64 dqb_isoftlimit;  /* preferred inode limit */
	__le64 dqb_curinodes;   /* current # allocated inodes */
	__le64 dqb_bhardlimit;  /* absolute limit on disk space
				 * (in QUOTABLOCK_SIZE) */
	__le64 dqb_bsoftlimit;  /* preferred limit on disk space
				 * (in QUOTABLOCK_SIZE) */
	__le64 dqb_curspace;    /* current space occupied (in bytes) */
	__le64 dqb_btime;       /* time limit for excessive disk use */
	__le64 dqb_itime;       /* time limit for excessive inode use */
};

static_assert(sizeof(struct v2r1_disk_dqblk) == 72, "");

#endif
