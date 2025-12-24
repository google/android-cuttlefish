/**
 * fsck.h
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _FSCK_H_
#define _FSCK_H_

#include "f2fs.h"

enum {
	FSCK_SUCCESS                 = 0,
	FSCK_ERROR_CORRECTED         = 1 << 0,
	FSCK_SYSTEM_SHOULD_REBOOT    = 1 << 1,
	FSCK_ERRORS_LEFT_UNCORRECTED = 1 << 2,
	FSCK_OPERATIONAL_ERROR       = 1 << 3,
	FSCK_USAGE_OR_SYNTAX_ERROR   = 1 << 4,
	FSCK_USER_CANCELLED          = 1 << 5,
	FSCK_SHARED_LIB_ERROR        = 1 << 7,
};

struct quota_ctx;

#define FSCK_UNMATCHED_EXTENT		0x00000001
#define FSCK_INLINE_INODE		0x00000002

enum {
	PREEN_MODE_0,
	PREEN_MODE_1,
	PREEN_MODE_2,
	PREEN_MODE_MAX
};

enum {
	NOERROR,
	EWRONG_OPT,
	ENEED_ARG,
	EUNKNOWN_OPT,
	EUNKNOWN_ARG,
};

enum SB_ADDR {
	SB0_ADDR = 0,
	SB1_ADDR,
	SB_MAX_ADDR,
};

#define SB_MASK(i)	(1 << (i))
#define SB_MASK_ALL	(SB_MASK(SB0_ADDR) | SB_MASK(SB1_ADDR))

/* fsck.c */
struct orphan_info {
	u32 nr_inodes;
	u32 *ino_list;
};

struct extent_info {
	u32 fofs;		/* start offset in a file */
	u32 blk;		/* start block address of the extent */
	u32 len;		/* length of the extent */
};

struct child_info {
	u32 state;
	u32 links;
	u32 files;
	u32 pgofs;
	u8 dot;
	u8 dotdot;
	u8 dir_level;
	u32 p_ino;		/* parent ino */
	char p_name[F2FS_NAME_LEN + 1]; /* parent name */
	u32 pp_ino;		/* parent parent ino*/
	struct extent_info ei;
	u32 last_blk;
	u32 i_namelen;  /* dentry namelen */
};

struct f2fs_dentry {
	char name[F2FS_NAME_LEN + 1];
	int depth;
	struct f2fs_dentry *next;
};

struct f2fs_fsck {
	struct f2fs_sb_info sbi;

	struct orphan_info orphani;
	struct chk_result {
		u64 checked_node_cnt;
		u64 valid_blk_cnt;
		u32 valid_nat_entry_cnt;
		u32 valid_node_cnt;
		u32 valid_inode_cnt;
		u32 multi_hard_link_files;
		u64 sit_valid_blocks;
		u32 sit_free_segs;
		u32 wp_fixed;
		u32 wp_inconsistent_zones;
	} chk;

	struct hard_link_node *hard_link_list_head;

	char *main_seg_usage;
	char *main_area_bitmap;
	char *nat_area_bitmap;
	char *sit_area_bitmap;

	u64 main_area_bitmap_sz;
	u32 nat_area_bitmap_sz;
	u32 sit_area_bitmap_sz;

	u64 nr_main_blks;
	u32 nr_nat_entries;

	u32 dentry_depth;
	struct f2fs_dentry *dentry;
	struct f2fs_dentry *dentry_end;
	struct f2fs_nat_entry *entries;
	u32 nat_valid_inode_cnt;

	struct quota_ctx *qctx;
};

enum NODE_TYPE {
	TYPE_INODE = 37,
	TYPE_DIRECT_NODE = 43,
	TYPE_INDIRECT_NODE = 53,
	TYPE_DOUBLE_INDIRECT_NODE = 67,
	TYPE_XATTR = 77
};

struct hard_link_node {
	u32 nid;
	u32 links;
	u32 actual_links;
	struct hard_link_node *next;
};

enum seg_type {
	SEG_TYPE_DATA,
	SEG_TYPE_CUR_DATA,
	SEG_TYPE_NODE,
	SEG_TYPE_CUR_NODE,
	SEG_TYPE_MAX,
};

struct selabel_handle;

static inline bool need_fsync_data_record(struct f2fs_sb_info *sbi)
{
	return !is_set_ckpt_flags(F2FS_CKPT(sbi), CP_UMOUNT_FLAG);
}

extern int fsck_chk_orphan_node(struct f2fs_sb_info *);
extern int fsck_chk_quota_node(struct f2fs_sb_info *);
extern int fsck_chk_quota_files(struct f2fs_sb_info *);
extern int fsck_sanity_check_nid(struct f2fs_sb_info *, u32,
					enum FILE_TYPE, enum NODE_TYPE);
extern int fsck_sanity_check_nat(struct f2fs_sb_info *sbi, u32 nid);
extern int fsck_chk_node_blk(struct f2fs_sb_info *, struct f2fs_inode *, u32,
		enum FILE_TYPE, enum NODE_TYPE, u32 *,
		struct f2fs_compr_blk_cnt *, struct child_info *);
extern int fsck_chk_root_inode(struct f2fs_sb_info *);
extern void fsck_chk_inode_blk(struct f2fs_sb_info *, u32, enum FILE_TYPE,
		struct f2fs_node *, u32 *, struct f2fs_compr_blk_cnt *,
		struct node_info *, struct child_info *);
extern int fsck_chk_dnode_blk(struct f2fs_sb_info *, struct f2fs_inode *,
		u32, enum FILE_TYPE, struct f2fs_node *, u32 *,
		struct f2fs_compr_blk_cnt *, struct child_info *,
		struct node_info *);
extern int fsck_chk_idnode_blk(struct f2fs_sb_info *, struct f2fs_inode *,
		enum FILE_TYPE, struct f2fs_node *, u32 *,
		struct f2fs_compr_blk_cnt *, struct child_info *);
extern int fsck_chk_didnode_blk(struct f2fs_sb_info *, struct f2fs_inode *,
		enum FILE_TYPE, struct f2fs_node *, u32 *,
		struct f2fs_compr_blk_cnt *, struct child_info *);
extern int fsck_chk_data_blk(struct f2fs_sb_info *, struct f2fs_inode *,
		u32, struct child_info *, int, enum FILE_TYPE, u32, u16, u8,
		struct f2fs_node *);
extern int fsck_chk_dentry_blk(struct f2fs_sb_info *, int,
		u32, struct child_info *, int, int, struct f2fs_node *);
int fsck_chk_inline_dentries(struct f2fs_sb_info *, struct f2fs_node *,
		struct child_info *);
void fsck_chk_checkpoint(struct f2fs_sb_info *sbi);
void fsck_update_sb_flags(struct f2fs_sb_info *sbi);
int fsck_chk_meta(struct f2fs_sb_info *sbi);
void fsck_chk_and_fix_write_pointers(struct f2fs_sb_info *);
int fsck_chk_curseg_info(struct f2fs_sb_info *);
void pretty_print_filename(const u8 *raw_name, u32 len,
			   char out[F2FS_PRINT_NAMELEN], int enc_name);

extern void update_free_segments(struct f2fs_sb_info *);
void print_cp_state(u32);
extern void print_node_info(struct f2fs_sb_info *, struct f2fs_node *, int);
extern void print_inode_info(struct f2fs_sb_info *, struct f2fs_node *, int);
extern struct seg_entry *get_seg_entry(struct f2fs_sb_info *, unsigned int);
extern struct f2fs_summary_block *get_sum_block(struct f2fs_sb_info *,
				unsigned int, int *);
extern int get_sum_entry(struct f2fs_sb_info *, u32, struct f2fs_summary *);
extern void update_sum_entry(struct f2fs_sb_info *, block_t,
				struct f2fs_summary *);
extern void get_node_info(struct f2fs_sb_info *, nid_t, struct node_info *);
extern void nullify_nat_entry(struct f2fs_sb_info *, u32);
extern void rewrite_sit_area_bitmap(struct f2fs_sb_info *);
extern void build_nat_area_bitmap(struct f2fs_sb_info *);
extern void build_sit_area_bitmap(struct f2fs_sb_info *);
extern int f2fs_set_main_bitmap(struct f2fs_sb_info *, u32, int);
extern int f2fs_clear_main_bitmap(struct f2fs_sb_info *, u32);
extern int f2fs_set_sit_bitmap(struct f2fs_sb_info *, u32);
extern int f2fs_clear_sit_bitmap(struct f2fs_sb_info *, u32);
extern void fsck_init(struct f2fs_sb_info *);
extern int fsck_verify(struct f2fs_sb_info *);
extern void fsck_free(struct f2fs_sb_info *);
extern bool f2fs_is_valid_blkaddr(struct f2fs_sb_info *sbi,
					block_t blkaddr, int type);
extern int f2fs_ra_meta_pages(struct f2fs_sb_info *, block_t, int, int);
extern int f2fs_do_mount(struct f2fs_sb_info *);
extern void f2fs_do_umount(struct f2fs_sb_info *);
extern int f2fs_sparse_initialize_meta(struct f2fs_sb_info *);

extern void flush_journal_entries(struct f2fs_sb_info *);
extern void update_curseg_info(struct f2fs_sb_info *, int);
extern void zero_journal_entries(struct f2fs_sb_info *);
extern void flush_sit_entries(struct f2fs_sb_info *);
extern void move_curseg_info(struct f2fs_sb_info *, u64, int);
extern void move_one_curseg_info(struct f2fs_sb_info *sbi, u64 from, int left,
				 int i);
extern void write_curseg_info(struct f2fs_sb_info *);
extern void save_curseg_warm_node_info(struct f2fs_sb_info *);
extern void restore_curseg_warm_node_info(struct f2fs_sb_info *);
extern int find_next_free_block(struct f2fs_sb_info *, u64 *, int, int, bool);
extern void duplicate_checkpoint(struct f2fs_sb_info *);
extern void write_checkpoint(struct f2fs_sb_info *);
extern void write_checkpoints(struct f2fs_sb_info *);
extern void write_raw_cp_blocks(struct f2fs_sb_info *sbi,
			struct f2fs_checkpoint *cp, int which);
extern void update_superblock(struct f2fs_super_block *, int);
extern void update_data_blkaddr(struct f2fs_sb_info *, nid_t, u16, block_t,
			struct f2fs_node *);
extern void update_nat_blkaddr(struct f2fs_sb_info *, nid_t, nid_t, block_t);

extern void print_raw_sb_info(struct f2fs_super_block *);
extern void print_ckpt_info(struct f2fs_sb_info *);
extern bool is_checkpoint_stop(struct f2fs_super_block *, bool);
extern bool is_inconsistent_error(struct f2fs_super_block *);
extern pgoff_t current_nat_addr(struct f2fs_sb_info *, nid_t, int *);

extern u32 get_free_segments(struct f2fs_sb_info *);
extern void get_current_sit_page(struct f2fs_sb_info *,
			unsigned int, struct f2fs_sit_block *);
extern void rewrite_current_sit_page(struct f2fs_sb_info *, unsigned int,
			struct f2fs_sit_block *);

extern u32 update_nat_bits_flags(struct f2fs_super_block *,
				struct f2fs_checkpoint *, u32);
extern void write_nat_bits(struct f2fs_sb_info *, struct f2fs_super_block *,
			struct f2fs_checkpoint *, int);
extern unsigned int get_usable_seg_count(struct f2fs_sb_info *);
extern bool is_usable_seg(struct f2fs_sb_info *, unsigned int);

/* dump.c */
struct dump_option {
	nid_t nid;
	nid_t start_nat;
	nid_t end_nat;
	int start_sit;
	int end_sit;
	int start_ssa;
	int end_ssa;
	int32_t blk_addr;
	nid_t scan_nid;
	int use_root_nid;
	char *base_path;
};

extern void nat_dump(struct f2fs_sb_info *, nid_t, nid_t);
extern void sit_dump(struct f2fs_sb_info *, unsigned int, unsigned int);
extern void ssa_dump(struct f2fs_sb_info *, int, int);
extern int dump_node(struct f2fs_sb_info *, nid_t, int, char *, int, int, char *);
extern int dump_info_from_blkaddr(struct f2fs_sb_info *, u32);
extern unsigned int start_bidx_of_node(unsigned int, struct f2fs_node *);
extern void dump_node_scan_disk(struct f2fs_sb_info *sbi, nid_t nid);
extern bool is_sit_bitmap_set(struct f2fs_sb_info *sbi, u32 blk_addr);

/* defrag.c */
int f2fs_defragment(struct f2fs_sb_info *, u64, u64, u64, int);

/* resize.c */
int f2fs_resize(struct f2fs_sb_info *);

/* sload.c */
int f2fs_sload(struct f2fs_sb_info *);

/* segment.c */
int reserve_new_block(struct f2fs_sb_info *, block_t *,
					struct f2fs_summary *, int, bool);
int new_data_block(struct f2fs_sb_info *, void *,
					struct dnode_of_data *, int);
int f2fs_build_file(struct f2fs_sb_info *, struct dentry *);
void f2fs_alloc_nid(struct f2fs_sb_info *, nid_t *);
void set_data_blkaddr(struct dnode_of_data *);
block_t new_node_block(struct f2fs_sb_info *,
					struct dnode_of_data *, unsigned int);
int f2fs_rebuild_qf_inode(struct f2fs_sb_info *sbi, int qtype);
int update_block(struct f2fs_sb_info *sbi, void *buf, u32 *blkaddr,
					struct f2fs_node *node_blk);

/* segment.c */
struct quota_file;
u64 f2fs_quota_size(struct quota_file *);
u64 f2fs_read(struct f2fs_sb_info *, nid_t, u8 *, u64, pgoff_t);
enum wr_addr_type {
	WR_NORMAL = 1,
	WR_COMPRESS_DATA = 2,
	WR_NULL_ADDR = NULL_ADDR,		/* 0 */
	WR_NEW_ADDR = NEW_ADDR,			/* -1U */
	WR_COMPRESS_ADDR = COMPRESS_ADDR,	/* -2U */
};
u64 f2fs_write(struct f2fs_sb_info *, nid_t, u8 *, u64, pgoff_t);
u64 f2fs_write_compress_data(struct f2fs_sb_info *, nid_t, u8 *, u64, pgoff_t);
u64 f2fs_write_addrtag(struct f2fs_sb_info *, nid_t, pgoff_t, unsigned int);
void f2fs_filesize_update(struct f2fs_sb_info *, nid_t, u64);

int get_dnode_of_data(struct f2fs_sb_info *, struct dnode_of_data *,
					pgoff_t, int);
void make_dentry_ptr(struct f2fs_dentry_ptr *, struct f2fs_node *, void *, int);
int f2fs_create(struct f2fs_sb_info *, struct dentry *);
int f2fs_mkdir(struct f2fs_sb_info *, struct dentry *);
int f2fs_symlink(struct f2fs_sb_info *, struct dentry *);
int inode_set_selinux(struct f2fs_sb_info *, u32, const char *);
int f2fs_find_path(struct f2fs_sb_info *, char *, nid_t *);
nid_t f2fs_lookup(struct f2fs_sb_info *, struct f2fs_node *, u8 *, int);
int f2fs_add_link(struct f2fs_sb_info *, struct f2fs_node *,
		const unsigned char *, int, nid_t, int, block_t *, int);
struct hardlink_cache_entry *f2fs_search_hardlink(struct f2fs_sb_info *sbi,
						struct dentry *de);

/* xattr.c */
void *read_all_xattrs(struct f2fs_sb_info *, struct f2fs_node *, bool);
void write_all_xattrs(struct f2fs_sb_info *sbi,
		struct f2fs_node *inode, __u32 hsize, void *txattr_addr);

/* dir.c */
int convert_inline_dentry(struct f2fs_sb_info *sbi, struct f2fs_node *node,
		block_t *p_blkaddr);

/* node.c */
int update_inode(struct f2fs_sb_info *sbi, struct f2fs_node *inode,
		u32 *blkaddr);

/* mount.c */
int flush_nat_journal_entries(struct f2fs_sb_info *sbi);
int flush_sit_journal_entries(struct f2fs_sb_info *sbi);

/* main.c */
int is_digits(char *optarg);

#endif /* _FSCK_H_ */
