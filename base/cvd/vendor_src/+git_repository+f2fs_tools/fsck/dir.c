/**
 * dir.c
 *
 * Many parts of codes are copied from Linux kernel/fs/f2fs.
 *
 * Copyright (C) 2015 Huawei Ltd.
 * Witten by:
 *   Hou Pengyang <houpengyang@huawei.com>
 *   Liu Shuoran <liushuoran@huawei.com>
 *   Jaegeuk Kim <jaegeuk@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "fsck.h"
#include "node.h"
#include <search.h>

static int room_for_filename(const u8 *bitmap, int slots, int max_slots)
{
	int bit_start = 0;
	int zero_start, zero_end;
next:
	zero_start = find_next_zero_bit_le(bitmap, max_slots, bit_start);
	if (zero_start >= max_slots)
		return max_slots;

	zero_end = find_next_bit_le(bitmap, max_slots, zero_start + 1);

	if (zero_end - zero_start >= slots)
		return zero_start;
	bit_start = zero_end;
	goto next;

}

void make_dentry_ptr(struct f2fs_dentry_ptr *d, struct f2fs_node *node_blk,
							void *src, int type)
{
	if (type == 1) {
		struct f2fs_dentry_block *t = (struct f2fs_dentry_block *)src;
		d->max = NR_DENTRY_IN_BLOCK;
		d->nr_bitmap = SIZE_OF_DENTRY_BITMAP;
		d->bitmap = t->dentry_bitmap;
		d->dentry = F2FS_DENTRY_BLOCK_DENTRIES(t);
		d->filename = F2FS_DENTRY_BLOCK_FILENAMES(t);
	} else {
		int entry_cnt = NR_INLINE_DENTRY(node_blk);
		int bitmap_size = INLINE_DENTRY_BITMAP_SIZE(node_blk);
		int reserved_size = INLINE_RESERVED_SIZE(node_blk);

		d->max = entry_cnt;
		d->nr_bitmap = bitmap_size;
		d->bitmap = (u8 *)src;
		d->dentry = (struct f2fs_dir_entry *)
				((char *)src + bitmap_size + reserved_size);
		d->filename = (__u8 (*)[F2FS_SLOT_LEN])((char *)src +
				bitmap_size + reserved_size +
				SIZE_OF_DIR_ENTRY * entry_cnt);
	}
}

static struct f2fs_dir_entry *find_target_dentry(const u8 *name,
		unsigned int len, f2fs_hash_t namehash, int *max_slots,
		struct f2fs_dentry_ptr *d)
{
	struct f2fs_dir_entry *de;
	unsigned long bit_pos = 0;
	int max_len = 0;

	if (max_slots)
		*max_slots = 0;
	while (bit_pos < (unsigned long)d->max) {
		if (!test_bit_le(bit_pos, d->bitmap)) {
			bit_pos++;
			max_len++;
			continue;
		}

		de = &d->dentry[bit_pos];
		if (le16_to_cpu(de->name_len) == len &&
			de->hash_code == namehash &&
			!memcmp(d->filename[bit_pos], name, len)) {
			goto found;
		}

		if (max_slots && max_len > *max_slots)
			*max_slots = max_len;
		max_len = 0;
		bit_pos += GET_DENTRY_SLOTS(le16_to_cpu(de->name_len));
	}
	de = NULL;
found:
	if (max_slots && max_len > *max_slots)
		*max_slots = max_len;
	return de;
}

static struct f2fs_dir_entry *find_in_block(void *block,
		const u8 *name, int len, f2fs_hash_t namehash,
		int *max_slots)
{
	struct f2fs_dentry_ptr d;

	make_dentry_ptr(&d, NULL, block, 1);
	return find_target_dentry(name, len, namehash, max_slots, &d);
}

static int find_in_level(struct f2fs_sb_info *sbi, struct f2fs_node *dir,
		unsigned int level, struct dentry *de)
{
	unsigned int nbucket, nblock;
	unsigned int bidx, end_block;
	struct f2fs_dir_entry *dentry = NULL;
	struct dnode_of_data dn;
	void *dentry_blk;
	int max_slots = 214;
	nid_t ino = le32_to_cpu(F2FS_NODE_FOOTER(dir)->ino);
	f2fs_hash_t namehash;
	unsigned int dir_level = dir->i.i_dir_level;
	int ret = 0;

	namehash = f2fs_dentry_hash(get_encoding(sbi), IS_CASEFOLDED(&dir->i),
					de->name, de->len);

	nbucket = dir_buckets(level, dir_level);
	nblock = bucket_blocks(level);

	bidx = dir_block_index(level, dir_level, le32_to_cpu(namehash) % nbucket);
	end_block = bidx + nblock;

	dentry_blk = calloc(F2FS_BLKSIZE, 1);
	ASSERT(dentry_blk);

	memset(&dn, 0, sizeof(dn));
	for (; bidx < end_block; bidx++) {

		/* Firstly, we should know direct node of target data blk */
		if (dn.node_blk && dn.node_blk != dn.inode_blk)
			free(dn.node_blk);

		set_new_dnode(&dn, dir, NULL, ino);
		get_dnode_of_data(sbi, &dn, bidx, LOOKUP_NODE);
		if (dn.data_blkaddr == NULL_ADDR)
			continue;

		ret = dev_read_block(dentry_blk, dn.data_blkaddr);
		ASSERT(ret >= 0);

		dentry = find_in_block(dentry_blk, de->name, de->len,
						namehash, &max_slots);
		if (dentry) {
			ret = 1;
			de->ino = le32_to_cpu(dentry->ino);
			break;
		}
	}

	if (dn.node_blk && dn.node_blk != dn.inode_blk)
		free(dn.node_blk);
	free(dentry_blk);

	return ret;
}

static int f2fs_find_entry(struct f2fs_sb_info *sbi,
				struct f2fs_node *dir, struct dentry *de)
{
	unsigned int max_depth;
	unsigned int level;

	max_depth = le32_to_cpu(dir->i.i_current_depth);
	for (level = 0; level < max_depth; level ++) {
		if (find_in_level(sbi, dir, level, de))
			return 1;
	}
	return 0;
}

/* return ino if file exists, otherwise return 0 */
nid_t f2fs_lookup(struct f2fs_sb_info *sbi, struct f2fs_node *dir,
				u8 *name, int len)
{
	int err;
	struct dentry de = {
		.name = name,
		.len = len,
	};

	err = f2fs_find_entry(sbi, dir, &de);
	if (err == 1)
		return de.ino;
	else
		return 0;
}

static void f2fs_update_dentry(nid_t ino, int file_type,
		struct f2fs_dentry_ptr *d,
		const unsigned char *name, int len, f2fs_hash_t name_hash,
		unsigned int bit_pos)
{
	struct f2fs_dir_entry *de;
	int slots = GET_DENTRY_SLOTS(len);
	int i;

	de = &d->dentry[bit_pos];
	de->name_len = cpu_to_le16(len);
	de->hash_code = name_hash;
	memcpy(d->filename[bit_pos], name, len);
	d->filename[bit_pos][len] = 0;
	de->ino = cpu_to_le32(ino);
	de->file_type = file_type;
	for (i = 0; i < slots; i++)
		test_and_set_bit_le(bit_pos + i, d->bitmap);
}

/*
 * f2fs_add_link - Add a new file(dir) to parent dir.
 */
int f2fs_add_link(struct f2fs_sb_info *sbi, struct f2fs_node *parent,
			const unsigned char *name, int name_len, nid_t ino,
			int file_type, block_t *p_blkaddr, int inc_link)
{
	int level = 0, current_depth, bit_pos;
	int nbucket, nblock, bidx, block;
	int slots = GET_DENTRY_SLOTS(name_len);
	f2fs_hash_t dentry_hash;
	struct f2fs_dentry_block *dentry_blk;
	struct f2fs_dentry_ptr d;
	struct dnode_of_data dn;
	nid_t pino;
	unsigned int dir_level;
	int ret;
	bool datablk_alloced = false;

	if (parent == NULL)
		return -EINVAL;

	dentry_hash = f2fs_dentry_hash(get_encoding(sbi),
						IS_CASEFOLDED(&parent->i),
						name, name_len);
	pino = le32_to_cpu(F2FS_NODE_FOOTER(parent)->ino);
	dir_level = parent->i.i_dir_level;

	if (!pino) {
		ERR_MSG("Wrong parent ino:%d \n", pino);
		return -EINVAL;
	}

	dentry_blk = calloc(F2FS_BLKSIZE, 1);
	ASSERT(dentry_blk);

	current_depth = le32_to_cpu(parent->i.i_current_depth);
start:
	if (current_depth == MAX_DIR_HASH_DEPTH) {
		free(dentry_blk);
		ERR_MSG("\tError: MAX_DIR_HASH\n");
		return -ENOSPC;
	}

	/* Need a new dentry block */
	if (level == current_depth)
		++current_depth;

	nbucket = dir_buckets(level, dir_level);
	nblock = bucket_blocks(level);
	bidx = dir_block_index(level, dir_level, le32_to_cpu(dentry_hash) % nbucket);

	memset(&dn, 0, sizeof(dn));
	for (block = bidx; block <= (bidx + nblock - 1); block++) {

		/* Firstly, we should know the direct node of target data blk */
		if (dn.node_blk && dn.node_blk != dn.inode_blk)
			free(dn.node_blk);

		set_new_dnode(&dn, parent, NULL, pino);
		get_dnode_of_data(sbi, &dn, block, ALLOC_NODE);

		if (dn.data_blkaddr == NULL_ADDR) {
			new_data_block(sbi, dentry_blk, &dn, CURSEG_HOT_DATA);
			datablk_alloced = true;
		} else {
			ret = dev_read_block(dentry_blk, dn.data_blkaddr);
			ASSERT(ret >= 0);
		}
		bit_pos = room_for_filename(dentry_blk->dentry_bitmap,
				slots, NR_DENTRY_IN_BLOCK);

		if (bit_pos < NR_DENTRY_IN_BLOCK)
			goto add_dentry;
	}
	level ++;
	goto start;

add_dentry:
	make_dentry_ptr(&d, NULL, (void *)dentry_blk, 1);
	f2fs_update_dentry(ino, file_type, &d, name, name_len, dentry_hash, bit_pos);

	if (c.zoned_model == F2FS_ZONED_HM) {
		if (datablk_alloced) {
			/* dentry uses hot data segment */
			ret = dev_write_block(dentry_blk, dn.data_blkaddr,
				f2fs_io_type_to_rw_hint(CURSEG_HOT_DATA));
		} else {
			ret = update_block(sbi, dentry_blk, &dn.data_blkaddr,
					dn.node_blk);
			if (dn.inode_blk == dn.node_blk)
				dn.idirty = 1;
			else
				dn.ndirty = 1;
		}
	} else {
		/* dentry uses hot data segment */
		ret = dev_write_block(dentry_blk, dn.data_blkaddr,
				f2fs_io_type_to_rw_hint(CURSEG_HOT_DATA));
	}
	ASSERT(ret >= 0);

	/*
	 * Parent inode needs updating, because its inode info may be changed.
	 * such as i_current_depth and i_blocks.
	 */
	if (parent->i.i_current_depth != cpu_to_le32(current_depth)) {
		parent->i.i_current_depth = cpu_to_le32(current_depth);
		dn.idirty = 1;
	}

	/* Update parent's i_links info*/
	if (inc_link && (file_type == F2FS_FT_DIR)){
		u32 links = le32_to_cpu(parent->i.i_links);
		parent->i.i_links = cpu_to_le32(links + 1);
		dn.idirty = 1;
	}

	if ((__u64)((block + 1) * F2FS_BLKSIZE) >
					le64_to_cpu(parent->i.i_size)) {
		parent->i.i_size = cpu_to_le64((block + 1) * F2FS_BLKSIZE);
		dn.idirty = 1;
	}

	if (dn.ndirty) {
		struct seg_entry *se;

		/* get segment type for rw hint */
		se = get_seg_entry(sbi, GET_SEGNO(sbi, dn.node_blkaddr));
		ret = dn.alloced ?
			dev_write_block(dn.node_blk, dn.node_blkaddr,
					f2fs_io_type_to_rw_hint(se->type)) :
			update_block(sbi, dn.node_blk, &dn.node_blkaddr, NULL);
		ASSERT(ret >= 0);
	}

	if (dn.idirty) {
		ASSERT(parent == dn.inode_blk);
		ret = update_inode(sbi, dn.inode_blk, p_blkaddr);
		ASSERT(ret >= 0);
	}

	if (dn.node_blk != dn.inode_blk)
		free(dn.node_blk);
	free(dentry_blk);
	return 0;
}

static void make_empty_dir(struct f2fs_sb_info *sbi, struct f2fs_node *inode)
{
	struct f2fs_dentry_block *dent_blk;
	nid_t ino = le32_to_cpu(F2FS_NODE_FOOTER(inode)->ino);
	nid_t pino = le32_to_cpu(inode->i.i_pino);
	struct f2fs_summary sum;
	struct node_info ni;
	block_t blkaddr = NULL_ADDR;
	int ret;

	get_node_info(sbi, ino, &ni);

	dent_blk = calloc(F2FS_BLKSIZE, 1);
	ASSERT(dent_blk);

	F2FS_DENTRY_BLOCK_DENTRY(dent_blk, 0).hash_code = 0;
	F2FS_DENTRY_BLOCK_DENTRY(dent_blk, 0).ino = cpu_to_le32(ino);
	F2FS_DENTRY_BLOCK_DENTRY(dent_blk, 0).name_len = cpu_to_le16(1);
	F2FS_DENTRY_BLOCK_DENTRY(dent_blk, 0).file_type = F2FS_FT_DIR;
	memcpy(F2FS_DENTRY_BLOCK_FILENAME(dent_blk, 0), ".", 1);

	F2FS_DENTRY_BLOCK_DENTRY(dent_blk, 1).hash_code = 0;
	F2FS_DENTRY_BLOCK_DENTRY(dent_blk, 1).ino = cpu_to_le32(pino);
	F2FS_DENTRY_BLOCK_DENTRY(dent_blk, 1).name_len = cpu_to_le16(2);
	F2FS_DENTRY_BLOCK_DENTRY(dent_blk, 1).file_type = F2FS_FT_DIR;
	memcpy(F2FS_DENTRY_BLOCK_FILENAME(dent_blk, 1), "..", 2);

	test_and_set_bit_le(0, dent_blk->dentry_bitmap);
	test_and_set_bit_le(1, dent_blk->dentry_bitmap);

	set_summary(&sum, ino, 0, ni.version);
	ret = reserve_new_block(sbi, &blkaddr, &sum, CURSEG_HOT_DATA, 0);
	ASSERT(!ret);

	ret = dev_write_block(dent_blk, blkaddr,
			      f2fs_io_type_to_rw_hint(CURSEG_HOT_DATA));
	ASSERT(ret >= 0);

	inode->i.i_addr[get_extra_isize(inode)] = cpu_to_le32(blkaddr);
	free(dent_blk);
}

static void page_symlink(struct f2fs_sb_info *sbi, struct f2fs_node *inode,
					const char *symname, int symlen)
{
	nid_t ino = le32_to_cpu(F2FS_NODE_FOOTER(inode)->ino);
	struct f2fs_summary sum;
	struct node_info ni;
	char *data_blk;
	block_t blkaddr = NULL_ADDR;
	int ret;

	get_node_info(sbi, ino, &ni);

	/* store into inline_data */
	if ((unsigned long)(symlen + 1) <= MAX_INLINE_DATA(inode)) {
		inode->i.i_inline |= F2FS_INLINE_DATA;
		inode->i.i_inline |= F2FS_DATA_EXIST;
		memcpy(inline_data_addr(inode), symname, symlen);
		return;
	}

	data_blk = calloc(F2FS_BLKSIZE, 1);
	ASSERT(data_blk);

	memcpy(data_blk, symname, symlen);

	set_summary(&sum, ino, 0, ni.version);
	ret = reserve_new_block(sbi, &blkaddr, &sum, CURSEG_WARM_DATA, 0);
	ASSERT(!ret);

	ret = dev_write_block(data_blk, blkaddr,
			      f2fs_io_type_to_rw_hint(CURSEG_WARM_DATA));
	ASSERT(ret >= 0);

	inode->i.i_addr[get_extra_isize(inode)] = cpu_to_le32(blkaddr);
	free(data_blk);
}

static inline int is_extension_exist(const char *s,
					const char *sub)
{
	unsigned int slen = strlen(s);
	unsigned int  sublen = strlen(sub);
	int i;

	/*
	 * filename format of multimedia file should be defined as:
	 * "filename + '.' + extension + (optional: '.' + temp extension)".
	 */
	if (slen < sublen + 2)
		return 0;

	for (i = 1; i < slen - sublen; i++) {
		if (s[i] != '.')
			continue;
		if (!strncasecmp(s + i + 1, sub, sublen))
			return 1;
	}

	return 0;
}

static void set_file_temperature(struct f2fs_sb_info *sbi,
				struct f2fs_node *node_blk,
				const unsigned char *name)
{
	__u8 (*extlist)[F2FS_EXTENSION_LEN] = sbi->raw_super->extension_list;
	int i, cold_count, hot_count;

	cold_count = le32_to_cpu(sbi->raw_super->extension_count);
	hot_count = sbi->raw_super->hot_ext_count;

	for (i = 0; i < cold_count + hot_count; i++) {
		if (is_extension_exist((const char *)name,
					(const char *)extlist[i]))
			break;
	}

	if (i == cold_count + hot_count)
		return;

	if (i < cold_count)
		node_blk->i.i_advise |= FADVISE_COLD_BIT;
	else
		node_blk->i.i_advise |= FADVISE_HOT_BIT;
}

static void init_inode_block(struct f2fs_sb_info *sbi,
		struct f2fs_node *node_blk, struct dentry *de)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	mode_t mode = de->mode;
	int links = 1;
	unsigned int size;
	int blocks = 1;

	if (de->file_type == F2FS_FT_DIR) {
		mode |= S_IFDIR;
		size = F2FS_BLKSIZE;
		links++;
		blocks++;
	} else if (de->file_type == F2FS_FT_REG_FILE) {
#ifdef S_IFREG
		mode |= S_IFREG;
#else
		ASSERT(0);
#endif
		size = 0;
	} else if (de->file_type == F2FS_FT_SYMLINK) {
		ASSERT(de->link);
#ifdef S_IFLNK
		mode |= S_IFLNK;
#else
		ASSERT(0);
#endif
		size = strlen(de->link);
		if (size + 1 > MAX_INLINE_DATA(node_blk))
			blocks++;
	} else {
		ASSERT(0);
	}

	node_blk->i.i_mode = cpu_to_le16(mode);
	node_blk->i.i_advise = 0;
	node_blk->i.i_uid = cpu_to_le32(de->uid);
	node_blk->i.i_gid = cpu_to_le32(de->gid);
	node_blk->i.i_links = cpu_to_le32(links);
	node_blk->i.i_size = cpu_to_le32(size);
	node_blk->i.i_blocks = cpu_to_le32(blocks);
	node_blk->i.i_atime = cpu_to_le64(de->mtime);
	node_blk->i.i_ctime = cpu_to_le64(de->mtime);
	node_blk->i.i_mtime = cpu_to_le64(de->mtime);
	node_blk->i.i_atime_nsec = 0;
	node_blk->i.i_ctime_nsec = 0;
	node_blk->i.i_mtime_nsec = 0;
	node_blk->i.i_generation = 0;
	if (de->file_type == F2FS_FT_DIR)
		node_blk->i.i_current_depth = cpu_to_le32(1);
	else
		node_blk->i.i_current_depth = cpu_to_le32(0);
	node_blk->i.i_xattr_nid = 0;
	node_blk->i.i_flags = 0;
	node_blk->i.i_inline = F2FS_INLINE_XATTR;
	node_blk->i.i_pino = cpu_to_le32(de->pino);
	node_blk->i.i_namelen = cpu_to_le32(de->len);
	memcpy(node_blk->i.i_name, de->name, de->len);
	node_blk->i.i_name[de->len] = 0;

	if (c.feature & F2FS_FEATURE_EXTRA_ATTR) {
		node_blk->i.i_inline |= F2FS_EXTRA_ATTR;
		node_blk->i.i_extra_isize = cpu_to_le16(calc_extra_isize());
	}

	set_file_temperature(sbi, node_blk, de->name);

	F2FS_NODE_FOOTER(node_blk)->ino = cpu_to_le32(de->ino);
	F2FS_NODE_FOOTER(node_blk)->nid = cpu_to_le32(de->ino);
	F2FS_NODE_FOOTER(node_blk)->flag = 0;
	F2FS_NODE_FOOTER(node_blk)->cp_ver = ckpt->checkpoint_ver;
	set_cold_node(node_blk, S_ISDIR(mode));

	if (S_ISDIR(mode)) {
		make_empty_dir(sbi, node_blk);
	} else if (S_ISLNK(mode)) {
		page_symlink(sbi, node_blk, de->link, size);

		free(de->link);
		de->link = NULL;
	}

	if (c.feature & F2FS_FEATURE_INODE_CHKSUM)
		node_blk->i.i_inode_checksum =
			cpu_to_le32(f2fs_inode_chksum(node_blk));
}

int convert_inline_dentry(struct f2fs_sb_info *sbi, struct f2fs_node *node,
							block_t *p_blkaddr)
{
	struct f2fs_inode *inode = &(node->i);
	unsigned int dir_level = node->i.i_dir_level;
	nid_t ino = le32_to_cpu(F2FS_NODE_FOOTER(node)->ino);
	char inline_data[MAX_INLINE_DATA(node)];
	struct dnode_of_data dn;
	struct f2fs_dentry_ptr d;
	unsigned long bit_pos = 0;
	int ret = 0;
	bool datablk_alloced = false;

	if (!(inode->i_inline & F2FS_INLINE_DENTRY))
		return 0;

	memcpy(inline_data, inline_data_addr(node), MAX_INLINE_DATA(node));
	memset(inline_data_addr(node), 0, MAX_INLINE_DATA(node));
	inode->i_inline &= ~F2FS_INLINE_DENTRY;

	ret = update_block(sbi, node, p_blkaddr, NULL);
	ASSERT(ret >= 0);

	memset(&dn, 0, sizeof(dn));
	if (!dir_level) {
		struct f2fs_dentry_block *dentry_blk;
		struct f2fs_dentry_ptr src, dst;

		dentry_blk = calloc(F2FS_BLKSIZE, 1);
		ASSERT(dentry_blk);

		set_new_dnode(&dn, node, NULL, ino);
		get_dnode_of_data(sbi, &dn, 0, ALLOC_NODE);
		if (dn.data_blkaddr == NULL_ADDR) {
			new_data_block(sbi, dentry_blk, &dn, CURSEG_HOT_DATA);
			datablk_alloced = true;
		}

		make_dentry_ptr(&src, node, (void *)inline_data, 2);
		make_dentry_ptr(&dst, NULL, (void *)dentry_blk, 1);

		 /* copy data from inline dentry block to new dentry block */
		memcpy(dst.bitmap, src.bitmap, src.nr_bitmap);
		memset(dst.bitmap + src.nr_bitmap, 0,
					dst.nr_bitmap - src.nr_bitmap);

		memcpy(dst.dentry, src.dentry, SIZE_OF_DIR_ENTRY * src.max);
		memcpy(dst.filename, src.filename, src.max * F2FS_SLOT_LEN);

		ret = datablk_alloced ?
			dev_write_block(dentry_blk, dn.data_blkaddr,
					f2fs_io_type_to_rw_hint(CURSEG_HOT_DATA)) :
			update_block(sbi, dentry_blk, &dn.data_blkaddr, NULL);
		ASSERT(ret >= 0);

		MSG(1, "%s: copy inline entry to block\n", __func__);

		free(dentry_blk);
		return ret;
	}

	make_empty_dir(sbi, node);
	make_dentry_ptr(&d, node, (void *)inline_data, 2);

	while (bit_pos < (unsigned long)d.max) {
		struct f2fs_dir_entry *de;
		const unsigned char *filename;
		int namelen;

		if (!test_bit_le(bit_pos, d.bitmap)) {
			bit_pos++;
			continue;
		}

		de = &d.dentry[bit_pos];
		if (!de->name_len) {
			bit_pos++;
			continue;
		}

		filename = d.filename[bit_pos];
		namelen = le32_to_cpu(de->name_len);

		if (is_dot_dotdot(filename, namelen)) {
			bit_pos += GET_DENTRY_SLOTS(namelen);
			continue;
		}

		ret = f2fs_add_link(sbi, node, filename, namelen,
				le32_to_cpu(de->ino),
				de->file_type, p_blkaddr, 0);
		if (ret)
			MSG(0, "Convert file \"%s\" ERR=%d\n", filename, ret);
		else
			MSG(1, "%s: add inline entry to block\n", __func__);

		bit_pos += GET_DENTRY_SLOTS(namelen);
	}

	return 0;
}

static int cmp_from_devino(const void *a, const void *b) {
	u64 devino_a = ((struct hardlink_cache_entry*) a)->from_devino;
	u64 devino_b = ((struct hardlink_cache_entry*) b)->from_devino;

	return (devino_a > devino_b) - (devino_a < devino_b);
}

struct hardlink_cache_entry *f2fs_search_hardlink(struct f2fs_sb_info *sbi,
						struct dentry *de)
{
	struct hardlink_cache_entry *find_hardlink = NULL;
	struct hardlink_cache_entry *found_hardlink = NULL;
	void *search_result;

	/* This might be a hardlink, try to find it in the cache */
	find_hardlink = calloc(1, sizeof(struct hardlink_cache_entry));
	find_hardlink->from_devino = de->from_devino;

	search_result = tsearch(find_hardlink, &(sbi->hardlink_cache),
				cmp_from_devino);
	ASSERT(search_result != 0);

	found_hardlink = *(struct hardlink_cache_entry**) search_result;
	ASSERT(find_hardlink->from_devino == found_hardlink->from_devino);

	/* If it was already in the cache, free the entry we just created */
	if (found_hardlink != find_hardlink)
		free(find_hardlink);

	return found_hardlink;
}

int f2fs_create(struct f2fs_sb_info *sbi, struct dentry *de)
{
	struct f2fs_node *parent, *child;
	struct hardlink_cache_entry *found_hardlink = NULL;
	struct node_info ni, hardlink_ni;
	struct f2fs_summary sum;
	block_t blkaddr = NULL_ADDR;
	int ret;
	bool nodeblk_alloced = false;

	/* Find if there is a */
	get_node_info(sbi, de->pino, &ni);
	if (ni.blk_addr == NULL_ADDR) {
		MSG(0, "No parent directory pino=%x\n", de->pino);
		return -1;
	}

	if (de->from_devino)
		found_hardlink = f2fs_search_hardlink(sbi, de);

	parent = calloc(F2FS_BLKSIZE, 1);
	ASSERT(parent);

	ret = dev_read_block(parent, ni.blk_addr);
	ASSERT(ret >= 0);

	/* Must convert inline dentry before the following opertions */
	ret = convert_inline_dentry(sbi, parent, &ni.blk_addr);
	if (ret) {
		MSG(0, "Convert inline dentry for pino=%x failed.\n", de->pino);
		ret = -1;
		goto free_parent_dir;
	}

	ret = f2fs_find_entry(sbi, parent, de);
	if (ret) {
		MSG(0, "Skip the existing \"%s\" pino=%x ERR=%d\n",
					de->name, de->pino, ret);
		if (de->file_type == F2FS_FT_REG_FILE)
			de->ino = 0;
		ret = 0;
		goto free_parent_dir;
	}

	child = calloc(F2FS_BLKSIZE, 1);
	ASSERT(child);

	if (found_hardlink && found_hardlink->to_ino) {
		/*
		 * If we found this devino in the cache, we're creating a
		 * hard link.
		 */
		get_node_info(sbi, found_hardlink->to_ino, &hardlink_ni);
		if (hardlink_ni.blk_addr == NULL_ADDR) {
			MSG(1, "No original inode for hard link to_ino=%x\n",
				found_hardlink->to_ino);
			ret = -1;
			goto free_child_dir;
		}

		/* Use previously-recorded inode */
		de->ino = found_hardlink->to_ino;
		blkaddr = hardlink_ni.blk_addr;
		MSG(1, "Info: Creating \"%s\" as hard link to inode %d\n",
				de->path, de->ino);
	} else {
		f2fs_alloc_nid(sbi, &de->ino);
	}

	init_inode_block(sbi, child, de);

	ret = f2fs_add_link(sbi, parent, child->i.i_name,
				le32_to_cpu(child->i.i_namelen),
				le32_to_cpu(F2FS_NODE_FOOTER(child)->ino),
				map_de_type(le16_to_cpu(child->i.i_mode)),
				&ni.blk_addr, 1);
	if (ret) {
		MSG(0, "Skip the existing \"%s\" pino=%x ERR=%d\n",
					de->name, de->pino, ret);
		ret = 0;
		goto free_child_dir;
	}

	if (found_hardlink) {
		if (!found_hardlink->to_ino) {
			MSG(2, "Adding inode %d from %s to hardlink cache\n",
				de->ino, de->path);
			found_hardlink->to_ino = de->ino;
		} else {
			/* Replace child with original block */
			free(child);

			child = calloc(F2FS_BLKSIZE, 1);
			ASSERT(child);

			ret = dev_read_block(child, blkaddr);
			ASSERT(ret >= 0);

			/* Increment links and skip to writing block */
			child->i.i_links = cpu_to_le32(
					le32_to_cpu(child->i.i_links) + 1);
			MSG(2, "Number of links on inode %d is now %d\n",
				de->ino, le32_to_cpu(child->i.i_links));
			goto write_child_dir;
		}
	}

	/* write child */
	set_summary(&sum, de->ino, 0, ni.version);
	ret = reserve_new_block(sbi, &blkaddr, &sum, CURSEG_HOT_NODE, 1);
	nodeblk_alloced = true;
	ASSERT(!ret);

	/* update nat info */
	update_nat_blkaddr(sbi, de->ino, de->ino, blkaddr);

write_child_dir:
	ret = nodeblk_alloced ? dev_write_block(child, blkaddr,
			f2fs_io_type_to_rw_hint(CURSEG_HOT_NODE)) :
		update_block(sbi, child, &blkaddr, NULL);
	ASSERT(ret >= 0);

	update_free_segments(sbi);
	MSG(1, "Info: Create %s -> %s\n"
		"  -- ino=%x, type=%x, mode=%x, uid=%x, "
		"gid=%x, cap=%"PRIx64", size=%lu, link=%u "
		"blocks=%"PRIx64" pino=%x\n",
		de->full_path, de->path,
		de->ino, de->file_type, de->mode,
		de->uid, de->gid, de->capabilities, de->size,
		le32_to_cpu(child->i.i_links),
		le64_to_cpu(child->i.i_blocks),
		de->pino);
free_child_dir:
	free(child);
free_parent_dir:
	free(parent);
	return ret;
}

int f2fs_mkdir(struct f2fs_sb_info *sbi, struct dentry *de)
{
	return f2fs_create(sbi, de);
}

int f2fs_symlink(struct f2fs_sb_info *sbi, struct dentry *de)
{
	return f2fs_create(sbi, de);
}

int f2fs_find_path(struct f2fs_sb_info *sbi, char *path, nid_t *ino)
{
	struct f2fs_node *parent;
	struct node_info ni;
	struct dentry de;
	int err = 0;
	int ret;
	char *p;

	if (path[0] != '/')
		return -ENOENT;

	*ino = F2FS_ROOT_INO(sbi);
	parent = calloc(F2FS_BLKSIZE, 1);
	ASSERT(parent);

	p = strtok(path, "/");
	while (p) {
		de.name = (const u8 *)p;
		de.len = strlen(p);

		get_node_info(sbi, *ino, &ni);
		if (ni.blk_addr == NULL_ADDR) {
			err = -ENOENT;
			goto err;
		}
		ret = dev_read_block(parent, ni.blk_addr);
		ASSERT(ret >= 0);

		ret = f2fs_find_entry(sbi, parent, &de);
		if (!ret) {
			err = -ENOENT;
			goto err;
		}

		*ino = de.ino;
		p = strtok(NULL, "/");
	}
err:
	free(parent);
	return err;
}
