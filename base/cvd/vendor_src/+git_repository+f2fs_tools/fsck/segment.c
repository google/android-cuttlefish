/**
 * segment.c
 *
 * Many parts of codes are copied from Linux kernel/fs/f2fs.
 *
 * Copyright (C) 2015 Huawei Ltd.
 * Witten by:
 *   Hou Pengyang <houpengyang@huawei.com>
 *   Liu Shuoran <liushuoran@huawei.com>
 *   Jaegeuk Kim <jaegeuk@kernel.org>
 * Copyright (c) 2020 Google Inc.
 *   Robin Hsu <robinhsu@google.com>
 *  : add sload compression support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "fsck.h"
#include "node.h"
#include "quotaio.h"

int reserve_new_block(struct f2fs_sb_info *sbi, block_t *to,
			struct f2fs_summary *sum, int type, bool is_inode)
{
	struct f2fs_fsck *fsck = F2FS_FSCK(sbi);
	struct seg_entry *se;
	u64 blkaddr, offset;
	u64 old_blkaddr = *to;
	bool is_node = IS_NODESEG(type);
	int left = 0;

	if (old_blkaddr == NULL_ADDR) {
		if (c.func == FSCK) {
			if (fsck->chk.valid_blk_cnt >= sbi->user_block_count) {
				ERR_MSG("Not enough space\n");
				return -ENOSPC;
			}
			if (is_node && fsck->chk.valid_node_cnt >=
						sbi->total_node_count) {
				ERR_MSG("Not enough space for node block\n");
				return -ENOSPC;
			}
		} else {
			if (sbi->total_valid_block_count >=
						sbi->user_block_count) {
				ERR_MSG("Not enough space\n");
				return -ENOSPC;
			}
			if (is_node && sbi->total_valid_node_count >=
						sbi->total_node_count) {
				ERR_MSG("Not enough space for node block\n");
				return -ENOSPC;
			}
		}
	}

	blkaddr = SM_I(sbi)->main_blkaddr;

	if (le32_to_cpu(sbi->raw_super->feature) & F2FS_FEATURE_RO) {
		if (IS_NODESEG(type)) {
			type = CURSEG_HOT_NODE;
			blkaddr = __end_block_addr(sbi);
			left = 1;
		} else if (IS_DATASEG(type)) {
			type = CURSEG_HOT_DATA;
			blkaddr = SM_I(sbi)->main_blkaddr;
			left = 0;
		}
	}

	if (find_next_free_block(sbi, &blkaddr, left, type, false)) {
		ERR_MSG("Can't find free block");
		ASSERT(0);
	}

	se = get_seg_entry(sbi, GET_SEGNO(sbi, blkaddr));
	offset = OFFSET_IN_SEG(sbi, blkaddr);
	se->type = se->orig_type = type;
	if (se->valid_blocks == 0)
		SM_I(sbi)->free_segments--;
	se->valid_blocks++;
	f2fs_set_bit(offset, (char *)se->cur_valid_map);
	if (need_fsync_data_record(sbi)) {
		se->ckpt_type = type;
		se->ckpt_valid_blocks++;
		f2fs_set_bit(offset, (char *)se->ckpt_valid_map);
	}
	if (c.func == FSCK) {
		f2fs_set_main_bitmap(sbi, blkaddr, type);
		f2fs_set_sit_bitmap(sbi, blkaddr);
	}

	if (old_blkaddr == NULL_ADDR) {
		sbi->total_valid_block_count++;
		if (is_node) {
			sbi->total_valid_node_count++;
			if (is_inode)
				sbi->total_valid_inode_count++;
		}
		if (c.func == FSCK) {
			fsck->chk.valid_blk_cnt++;
			if (is_node) {
				fsck->chk.valid_nat_entry_cnt++;
				fsck->chk.valid_node_cnt++;
				if (is_inode)
					fsck->chk.valid_inode_cnt++;
			}
		}
	}
	se->dirty = 1;

	/* read/write SSA */
	*to = (block_t)blkaddr;
	update_sum_entry(sbi, *to, sum);

	return 0;
}

int new_data_block(struct f2fs_sb_info *sbi, void *block,
				struct dnode_of_data *dn, int type)
{
	struct f2fs_super_block *sb = F2FS_RAW_SUPER(sbi);
	struct f2fs_summary sum;
	struct node_info ni;
	unsigned int blkaddr = datablock_addr(dn->node_blk, dn->ofs_in_node);
	int ret;

	if ((get_sb(feature) & F2FS_FEATURE_RO) &&
					type != CURSEG_HOT_DATA)
		type = CURSEG_HOT_DATA;

	ASSERT(dn->node_blk);
	memset(block, 0, F2FS_BLKSIZE);

	get_node_info(sbi, dn->nid, &ni);
	set_summary(&sum, dn->nid, dn->ofs_in_node, ni.version);

	dn->data_blkaddr = blkaddr;
	ret = reserve_new_block(sbi, &dn->data_blkaddr, &sum, type, 0);
	if (ret) {
		c.alloc_failed = 1;
		return ret;
	}

	if (blkaddr == NULL_ADDR)
		inc_inode_blocks(dn);
	else if (blkaddr == NEW_ADDR)
		dn->idirty = 1;
	set_data_blkaddr(dn);
	return 0;
}

u64 f2fs_quota_size(struct quota_file *qf)
{
	struct node_info ni;
	struct f2fs_node *inode;
	u64 filesize;

	inode = (struct f2fs_node *) calloc(F2FS_BLKSIZE, 1);
	ASSERT(inode);

	/* Read inode */
	get_node_info(qf->sbi, qf->ino, &ni);
	ASSERT(dev_read_block(inode, ni.blk_addr) >= 0);
	ASSERT(S_ISREG(le16_to_cpu(inode->i.i_mode)));

	filesize = le64_to_cpu(inode->i.i_size);
	free(inode);
	return filesize;
}

u64 f2fs_read(struct f2fs_sb_info *sbi, nid_t ino, u8 *buffer,
					u64 count, pgoff_t offset)
{
	struct dnode_of_data dn;
	struct node_info ni;
	struct f2fs_node *inode;
	char *blk_buffer;
	u64 filesize;
	u64 off_in_blk;
	u64 len_in_blk;
	u64 read_count;
	u64 remained_blkentries;
	block_t blkaddr;
	void *index_node = NULL;

	memset(&dn, 0, sizeof(dn));

	/* Memory allocation for block buffer and inode. */
	blk_buffer = calloc(F2FS_BLKSIZE, 2);
	ASSERT(blk_buffer);
	inode = (struct f2fs_node*)(blk_buffer + F2FS_BLKSIZE);

	/* Read inode */
	get_node_info(sbi, ino, &ni);
	ASSERT(dev_read_block(inode, ni.blk_addr) >= 0);
	ASSERT(!S_ISDIR(le16_to_cpu(inode->i.i_mode)));
	ASSERT(!S_ISLNK(le16_to_cpu(inode->i.i_mode)));

	/* Adjust count with file length. */
	filesize = le64_to_cpu(inode->i.i_size);
	if (offset > filesize)
		count = 0;
	else if (count + offset > filesize)
		count = filesize - offset;

	/* Main loop for file blocks */
	read_count = remained_blkentries = 0;
	while (count > 0) {
		if (remained_blkentries == 0) {
			set_new_dnode(&dn, inode, NULL, ino);
			get_dnode_of_data(sbi, &dn, F2FS_BYTES_TO_BLK(offset),
					LOOKUP_NODE);
			if (index_node)
				free(index_node);
			index_node = (dn.node_blk == dn.inode_blk) ?
							NULL : dn.node_blk;
			remained_blkentries = ADDRS_PER_PAGE(sbi,
						dn.node_blk, dn.inode_blk);
		}
		ASSERT(remained_blkentries > 0);

		blkaddr = datablock_addr(dn.node_blk, dn.ofs_in_node);
		if (blkaddr == NULL_ADDR || blkaddr == NEW_ADDR)
			break;

		off_in_blk = offset % F2FS_BLKSIZE;
		len_in_blk = F2FS_BLKSIZE - off_in_blk;
		if (len_in_blk > count)
			len_in_blk = count;

		/* Read data from single block. */
		if (len_in_blk < F2FS_BLKSIZE) {
			ASSERT(dev_read_block(blk_buffer, blkaddr) >= 0);
			memcpy(buffer, blk_buffer + off_in_blk, len_in_blk);
		} else {
			/* Direct read */
			ASSERT(dev_read_block(buffer, blkaddr) >= 0);
		}

		offset += len_in_blk;
		count -= len_in_blk;
		buffer += len_in_blk;
		read_count += len_in_blk;

		dn.ofs_in_node++;
		remained_blkentries--;
	}
	if (index_node)
		free(index_node);
	free(blk_buffer);

	return read_count;
}

/*
 * Do not call this function directly.  Instead, call one of the following:
 *     u64 f2fs_write();
 *     u64 f2fs_write_compress_data();
 *     u64 f2fs_write_addrtag();
 */
static u64 f2fs_write_ex(struct f2fs_sb_info *sbi, nid_t ino, u8 *buffer,
		u64 count, pgoff_t offset, enum wr_addr_type addr_type)
{
	struct dnode_of_data dn;
	struct node_info ni;
	struct f2fs_node *inode;
	char *blk_buffer;
	void *wbuf;
	u64 off_in_blk;
	u64 len_in_blk;
	u64 written_count;
	u64 remained_blkentries;
	block_t blkaddr;
	void* index_node = NULL;
	int idirty = 0;
	int err, ret;
	bool datablk_alloced = false;
	bool has_data = (addr_type == WR_NORMAL
			|| addr_type == WR_COMPRESS_DATA);

	if (count == 0)
		return 0;

	/*
	 * Enforce calling from f2fs_write(), f2fs_write_compress_data(),
	 * and f2fs_write_addrtag().   Beside, check if is properly called.
	 */
	ASSERT((!has_data && buffer == NULL) || (has_data && buffer != NULL));
	if (addr_type != WR_NORMAL)
		ASSERT(offset % F2FS_BLKSIZE == 0); /* block boundary only */

	/* Memory allocation for block buffer and inode. */
	blk_buffer = calloc(F2FS_BLKSIZE, 2);
	ASSERT(blk_buffer);
	inode = (struct f2fs_node*)(blk_buffer + F2FS_BLKSIZE);

	/* Read inode */
	get_node_info(sbi, ino, &ni);
	ASSERT(dev_read_block(inode, ni.blk_addr) >= 0);
	ASSERT(!S_ISDIR(le16_to_cpu(inode->i.i_mode)));
	ASSERT(!S_ISLNK(le16_to_cpu(inode->i.i_mode)));

	/* Main loop for file blocks */
	written_count = remained_blkentries = 0;
	while (count > 0) {
		if (remained_blkentries == 0) {
			set_new_dnode(&dn, inode, NULL, ino);
			err = get_dnode_of_data(sbi, &dn,
					F2FS_BYTES_TO_BLK(offset), ALLOC_NODE);
			if (err)
				break;
			idirty |= dn.idirty;
			free(index_node);
			index_node = (dn.node_blk == dn.inode_blk) ?
					NULL : dn.node_blk;
			remained_blkentries = ADDRS_PER_PAGE(sbi,
					dn.node_blk, dn.inode_blk) -
					dn.ofs_in_node;
		}
		ASSERT(remained_blkentries > 0);

		if (!has_data) {
			struct seg_entry *se;

			se = get_seg_entry(sbi, GET_SEGNO(sbi, dn.node_blkaddr));
			dn.data_blkaddr = addr_type;
			set_data_blkaddr(&dn);
			idirty |= dn.idirty;
			if (dn.ndirty) {
				ret = dn.alloced ? dev_write_block(dn.node_blk,
					dn.node_blkaddr,
					f2fs_io_type_to_rw_hint(se->type)) :
					update_block(sbi, dn.node_blk,
					&dn.node_blkaddr, NULL);
				ASSERT(ret >= 0);
			}
			written_count = 0;
			break;
		}

		datablk_alloced = false;
		blkaddr = datablock_addr(dn.node_blk, dn.ofs_in_node);
		if (blkaddr == NULL_ADDR || blkaddr == NEW_ADDR) {
			err = new_data_block(sbi, blk_buffer,
						&dn, CURSEG_WARM_DATA);
			if (err)
				break;
			blkaddr = dn.data_blkaddr;
			idirty |= dn.idirty;
			datablk_alloced = true;
		}

		off_in_blk = offset % F2FS_BLKSIZE;
		len_in_blk = F2FS_BLKSIZE - off_in_blk;
		if (len_in_blk > count)
			len_in_blk = count;

		/* Write data to single block. */
		if (len_in_blk < F2FS_BLKSIZE) {
			ASSERT(dev_read_block(blk_buffer, blkaddr) >= 0);
			memcpy(blk_buffer + off_in_blk, buffer, len_in_blk);
			wbuf = blk_buffer;
		} else {
			/* Direct write */
			wbuf = buffer;
		}

		if (c.zoned_model == F2FS_ZONED_HM) {
			if (datablk_alloced) {
				ret = dev_write_block(wbuf, blkaddr,
					f2fs_io_type_to_rw_hint(CURSEG_WARM_DATA));
			} else {
				ret = update_block(sbi, wbuf, &blkaddr,
						dn.node_blk);
				if (dn.inode_blk == dn.node_blk)
					idirty = 1;
				else
					dn.ndirty = 1;
			}
		} else {
			ret = dev_write_block(wbuf, blkaddr,
					f2fs_io_type_to_rw_hint(CURSEG_WARM_DATA));
		}
		ASSERT(ret >= 0);

		offset += len_in_blk;
		count -= len_in_blk;
		buffer += len_in_blk;
		written_count += len_in_blk;

		dn.ofs_in_node++;
		if ((--remained_blkentries == 0 || count == 0) && (dn.ndirty)) {
			struct seg_entry *se;
			se = get_seg_entry(sbi, GET_SEGNO(sbi, dn.node_blkaddr));
			ret = dn.alloced ?
				dev_write_block(dn.node_blk, dn.node_blkaddr,
						f2fs_io_type_to_rw_hint(se->type)) :
				update_block(sbi, dn.node_blk, &dn.node_blkaddr, NULL);
			ASSERT(ret >= 0);
		}
	}

	if (addr_type == WR_NORMAL && offset > le64_to_cpu(inode->i.i_size)) {
		inode->i.i_size = cpu_to_le64(offset);
		idirty = 1;
	}
	if (idirty) {
		get_node_info(sbi, ino, &ni);
		ASSERT(inode == dn.inode_blk);
		ASSERT(update_inode(sbi, inode, &ni.blk_addr) >= 0);
	}

	free(index_node);
	free(blk_buffer);

	return written_count;
}

u64 f2fs_write(struct f2fs_sb_info *sbi, nid_t ino, u8 *buffer,
					u64 count, pgoff_t offset)
{
	return f2fs_write_ex(sbi, ino, buffer, count, offset, WR_NORMAL);
}

u64 f2fs_write_compress_data(struct f2fs_sb_info *sbi, nid_t ino, u8 *buffer,
					u64 count, pgoff_t offset)
{
	return f2fs_write_ex(sbi, ino, buffer, count, offset, WR_COMPRESS_DATA);
}

u64 f2fs_write_addrtag(struct f2fs_sb_info *sbi, nid_t ino, pgoff_t offset,
		unsigned int addrtag)
{
	ASSERT(addrtag == COMPRESS_ADDR || addrtag == NEW_ADDR
			|| addrtag == NULL_ADDR);
	return f2fs_write_ex(sbi, ino, NULL, F2FS_BLKSIZE, offset, addrtag);
}

/* This function updates only inode->i.i_size */
void f2fs_filesize_update(struct f2fs_sb_info *sbi, nid_t ino, u64 filesize)
{
	struct node_info ni;
	struct f2fs_node *inode;

	inode = calloc(F2FS_BLKSIZE, 1);
	ASSERT(inode);
	get_node_info(sbi, ino, &ni);

	ASSERT(dev_read_block(inode, ni.blk_addr) >= 0);
	ASSERT(!S_ISDIR(le16_to_cpu(inode->i.i_mode)));
	ASSERT(!S_ISLNK(le16_to_cpu(inode->i.i_mode)));

	inode->i.i_size = cpu_to_le64(filesize);

	ASSERT(update_inode(sbi, inode, &ni.blk_addr) >= 0);
	free(inode);
}

#define MAX_BULKR_RETRY 5
int bulkread(int fd, void *rbuf, size_t rsize, bool *eof)
{
	int n = 0;
	int retry = MAX_BULKR_RETRY;
	int cur;

	if (!rsize)
		return 0;

	if (eof != NULL)
		*eof = false;
	while (rsize && (cur = read(fd, rbuf, rsize)) != 0) {
		if (cur == -1) {
			if (errno == EINTR && retry--)
				continue;
			return -1;
		}
		retry = MAX_BULKR_RETRY;

		rsize -= cur;
		n += cur;
	}
	if (eof != NULL)
		*eof = (cur == 0);
	return n;
}

u64 f2fs_fix_mutable(struct f2fs_sb_info *sbi, nid_t ino, pgoff_t offset,
		unsigned int compressed)
{
	unsigned int i;
	u64 wlen;

	if (c.compress.readonly)
		return 0;

	for (i = 0; i < compressed - 1; i++) {
		wlen = f2fs_write_addrtag(sbi, ino,
				offset + (i << F2FS_BLKSIZE_BITS), NEW_ADDR);
		if (wlen)
			return wlen;
	}
	return 0;
}

static inline int is_consecutive(u32 prev_addr, u32 cur_addr)
{
	if (is_valid_data_blkaddr(cur_addr) && (cur_addr == prev_addr + 1))
		return 1;
	return 0;
}

static inline void copy_extent_info(struct extent_info *t_ext,
				struct extent_info *s_ext)
{
	t_ext->fofs = s_ext->fofs;
	t_ext->blk = s_ext->blk;
	t_ext->len = s_ext->len;
}

static inline void update_extent_info(struct f2fs_node *inode,
				struct extent_info *ext)
{
	inode->i.i_ext.fofs = cpu_to_le32(ext->fofs);
	inode->i.i_ext.blk_addr = cpu_to_le32(ext->blk);
	inode->i.i_ext.len = cpu_to_le32(ext->len);
}

static void update_largest_extent(struct f2fs_sb_info *sbi, nid_t ino)
{
	struct dnode_of_data dn;
	struct node_info ni;
	struct f2fs_node *inode;
	u32 blkaddr, prev_blkaddr, cur_blk = 0, end_blk;
	struct extent_info largest_ext = { 0, }, cur_ext = { 0, };
	u64 remained_blkentries = 0;
	u32 cluster_size;
	int count;
	void *index_node = NULL;

	memset(&dn, 0, sizeof(dn));
	largest_ext.len = cur_ext.len = 0;

	inode = (struct f2fs_node *) calloc(F2FS_BLKSIZE, 1);
	ASSERT(inode);

	/* Read inode info */
	get_node_info(sbi, ino, &ni);
	ASSERT(dev_read_block(inode, ni.blk_addr) >= 0);
	cluster_size = 1 << inode->i.i_log_cluster_size;

	if (inode->i.i_inline & F2FS_INLINE_DATA)
		goto exit;

	end_blk  = f2fs_max_file_offset(&inode->i) >> F2FS_BLKSIZE_BITS;

	while (cur_blk <= end_blk) {
		if (remained_blkentries == 0) {
			set_new_dnode(&dn, inode, NULL, ino);
			get_dnode_of_data(sbi, &dn, cur_blk, LOOKUP_NODE);
			if (index_node)
				free(index_node);
			index_node = (dn.node_blk == dn.inode_blk) ?
				NULL : dn.node_blk;
			remained_blkentries = ADDRS_PER_PAGE(sbi,
					dn.node_blk, dn.inode_blk);
		}
		ASSERT(remained_blkentries > 0);

		blkaddr = datablock_addr(dn.node_blk, dn.ofs_in_node);
		if (cur_ext.len > 0) {
			if (is_consecutive(prev_blkaddr, blkaddr))
				cur_ext.len++;
			else {
				if (cur_ext.len > largest_ext.len)
					copy_extent_info(&largest_ext,
							&cur_ext);
				cur_ext.len = 0;
			}
		}

		if (cur_ext.len == 0 && is_valid_data_blkaddr(blkaddr)) {
			cur_ext.fofs = cur_blk;
			cur_ext.len = 1;
			cur_ext.blk = blkaddr;
		}

		prev_blkaddr = blkaddr;
		count = blkaddr == COMPRESS_ADDR ? cluster_size : 1;
		cur_blk += count;
		dn.ofs_in_node += count;
		remained_blkentries -= count;
	}

exit:
	if (cur_ext.len > largest_ext.len)
		copy_extent_info(&largest_ext, &cur_ext);
	if (largest_ext.len > 0) {
		update_extent_info(inode, &largest_ext);
		ASSERT(update_inode(sbi, inode, &ni.blk_addr) >= 0);
	}

	if (index_node)
		free(index_node);
	free(inode);
}

int f2fs_build_file(struct f2fs_sb_info *sbi, struct dentry *de)
{
	int fd, n = -1;
	pgoff_t off = 0;
	u8 buffer[F2FS_BLKSIZE];
	struct node_info ni;
	struct f2fs_node *node_blk;

	if (de->ino == 0)
		return -1;

	if (de->from_devino) {
		struct hardlink_cache_entry *found_hardlink;

		found_hardlink = f2fs_search_hardlink(sbi, de);
		if (found_hardlink && found_hardlink->to_ino &&
				found_hardlink->nbuild)
			return 0;

		found_hardlink->nbuild++;
	}

	fd = open(de->full_path, O_RDONLY);
	if (fd < 0) {
		MSG(0, "Skip: Fail to open %s\n", de->full_path);
		return -1;
	}

	/* inline_data support */
	if (de->size <= DEF_MAX_INLINE_DATA) {
		int ret;

		get_node_info(sbi, de->ino, &ni);

		node_blk = calloc(F2FS_BLKSIZE, 1);
		ASSERT(node_blk);

		ret = dev_read_block(node_blk, ni.blk_addr);
		ASSERT(ret >= 0);

		node_blk->i.i_inline |= F2FS_INLINE_DATA;
		node_blk->i.i_inline |= F2FS_DATA_EXIST;

		if (c.feature & F2FS_FEATURE_EXTRA_ATTR) {
			node_blk->i.i_inline |= F2FS_EXTRA_ATTR;
			node_blk->i.i_extra_isize =
					cpu_to_le16(calc_extra_isize());
		}
		n = read(fd, buffer, F2FS_BLKSIZE);
		ASSERT((unsigned long)n == de->size);
		memcpy(inline_data_addr(node_blk), buffer, de->size);
		node_blk->i.i_size = cpu_to_le64(de->size);
		ASSERT(update_inode(sbi, node_blk, &ni.blk_addr) >= 0);
		free(node_blk);
#ifdef WITH_SLOAD
	} else if (c.func == SLOAD && c.compress.enabled &&
			c.compress.filter_ops->filter(de->full_path)) {
		bool eof = false;
		u8 *rbuf = c.compress.cc.rbuf;
		unsigned int cblocks = 0;

		node_blk = calloc(F2FS_BLKSIZE, 1);
		ASSERT(node_blk);

		/* read inode */
		get_node_info(sbi, de->ino, &ni);
		ASSERT(dev_read_block(node_blk, ni.blk_addr) >= 0);
		/* update inode meta */
		node_blk->i.i_compress_algorithm = c.compress.alg;
		node_blk->i.i_log_cluster_size =
				c.compress.cc.log_cluster_size;
		node_blk->i.i_flags = cpu_to_le32(F2FS_COMPR_FL);
		if (c.compress.readonly)
			node_blk->i.i_inline |= F2FS_COMPRESS_RELEASED;
		ASSERT(update_inode(sbi, node_blk, &ni.blk_addr) >= 0);

		while (!eof && (n = bulkread(fd, rbuf, c.compress.cc.rlen,
				&eof)) > 0) {
			int ret = c.compress.ops->compress(&c.compress.cc);
			u64 wlen;
			u32 csize = ALIGN_UP(c.compress.cc.clen +
					COMPRESS_HEADER_SIZE, F2FS_BLKSIZE);
			unsigned int cur_cblk;

			if (ret || n < c.compress.cc.rlen ||
				n < (int)(csize + F2FS_BLKSIZE *
						c.compress.min_blocks)) {
				wlen = f2fs_write(sbi, de->ino, rbuf, n, off);
				ASSERT((int)wlen == n);
			} else {
				wlen = f2fs_write_addrtag(sbi, de->ino, off,
						WR_COMPRESS_ADDR);
				ASSERT(!wlen);
				wlen = f2fs_write_compress_data(sbi, de->ino,
						(u8 *)c.compress.cc.cbuf,
						csize, off + F2FS_BLKSIZE);
				ASSERT(wlen == csize);
				c.compress.ops->reset(&c.compress.cc);
				cur_cblk = (c.compress.cc.rlen - csize) /
								F2FS_BLKSIZE;
				cblocks += cur_cblk;
				wlen = f2fs_fix_mutable(sbi, de->ino,
						off + F2FS_BLKSIZE + csize,
						cur_cblk);
				ASSERT(!wlen);
			}
			off += n;
		}
		if (n == -1) {
			fprintf(stderr, "Load file '%s' failed: ",
					de->full_path);
			perror(NULL);
		}
		/* read inode */
		get_node_info(sbi, de->ino, &ni);
		ASSERT(dev_read_block(node_blk, ni.blk_addr) >= 0);
		/* update inode meta */
		node_blk->i.i_size = cpu_to_le64(off);
		if (!c.compress.readonly) {
			node_blk->i.i_compr_blocks = cpu_to_le64(cblocks);
			node_blk->i.i_blocks += cpu_to_le64(cblocks);
		}
		ASSERT(update_inode(sbi, node_blk, &ni.blk_addr) >= 0);
		free(node_blk);

		if (!c.compress.readonly) {
			sbi->total_valid_block_count += cblocks;
			if (sbi->total_valid_block_count >=
					sbi->user_block_count) {
				ERR_MSG("Not enough space\n");
				ASSERT(0);
			}
		}
#endif
	} else {
		while ((n = read(fd, buffer, F2FS_BLKSIZE)) > 0) {
			f2fs_write(sbi, de->ino, buffer, n, off);
			off += n;
		}
	}

	close(fd);
	if (n < 0)
		return -1;

	if (!c.compress.enabled || (c.feature & F2FS_FEATURE_RO))
		update_largest_extent(sbi, de->ino);
	update_free_segments(sbi);

	MSG(1, "Info: Create %s -> %s\n"
		"  -- ino=%x, type=%x, mode=%x, uid=%x, "
		"gid=%x, cap=%"PRIx64", size=%lu, pino=%x\n",
		de->full_path, de->path,
		de->ino, de->file_type, de->mode,
		de->uid, de->gid, de->capabilities, de->size, de->pino);
	return 0;
}

int update_block(struct f2fs_sb_info *sbi, void *buf, u32 *blkaddr,
		struct f2fs_node *node_blk)
{
	struct seg_entry *se;
	struct f2fs_summary sum;
	u64 new_blkaddr, old_blkaddr = *blkaddr, offset;
	int ret, type;

	if (c.zoned_model != F2FS_ZONED_HM)
		return dev_write_block(buf, old_blkaddr, WRITE_LIFE_NONE);

	/* update sit bitmap & valid_blocks && se->type for old block*/
	se = get_seg_entry(sbi, GET_SEGNO(sbi, old_blkaddr));
	offset = OFFSET_IN_SEG(sbi, old_blkaddr);
	type = se->type;
	se->valid_blocks--;
	f2fs_clear_bit(offset, (char *)se->cur_valid_map);
	if (need_fsync_data_record(sbi))
		f2fs_clear_bit(offset, (char *)se->ckpt_valid_map);
	se->dirty = 1;
	f2fs_clear_main_bitmap(sbi, old_blkaddr);
	f2fs_clear_sit_bitmap(sbi, old_blkaddr);

	new_blkaddr = SM_I(sbi)->main_blkaddr;
	if (find_next_free_block(sbi, &new_blkaddr, 0, type, false)) {
		ERR_MSG("Can't find free block for the update");
		ASSERT(0);
	}

	ret = dev_write_block(buf, new_blkaddr, f2fs_io_type_to_rw_hint(type));
	ASSERT(ret >= 0);

	*blkaddr = new_blkaddr;

	/* update sit bitmap & valid_blocks && se->type for new block */
	se = get_seg_entry(sbi, GET_SEGNO(sbi, new_blkaddr));
	offset = OFFSET_IN_SEG(sbi, new_blkaddr);
	se->type = se->orig_type = type;
	se->valid_blocks++;
	f2fs_set_bit(offset, (char *)se->cur_valid_map);
	if (need_fsync_data_record(sbi))
		f2fs_set_bit(offset, (char *)se->ckpt_valid_map);
	se->dirty = 1;
	f2fs_set_main_bitmap(sbi, new_blkaddr, type);
	f2fs_set_sit_bitmap(sbi, new_blkaddr);

	/* update SSA */
	get_sum_entry(sbi, old_blkaddr, &sum);
	update_sum_entry(sbi, new_blkaddr, &sum);

	if (IS_DATASEG(type)) {
		update_data_blkaddr(sbi, le32_to_cpu(sum.nid),
				le16_to_cpu(sum.ofs_in_node), new_blkaddr, node_blk);
	} else
		update_nat_blkaddr(sbi, 0, le32_to_cpu(sum.nid), new_blkaddr);

	DBG(1, "Update %s block %"PRIx64" -> %"PRIx64"\n",
		IS_DATASEG(type) ? "data" : "node", old_blkaddr, new_blkaddr);
	return ret;
}
