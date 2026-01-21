/**
 * xattr.c
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
#include "xattr.h"

void *read_all_xattrs(struct f2fs_sb_info *sbi, struct f2fs_node *inode,
			bool sanity_check)
{
	struct f2fs_xattr_header *header;
	void *txattr_addr;
	u64 inline_size = inline_xattr_size(&inode->i);
	nid_t xnid = le32_to_cpu(inode->i.i_xattr_nid);

	if (c.func == FSCK && xnid && sanity_check) {
		if (fsck_sanity_check_nid(sbi, xnid, F2FS_FT_XATTR, TYPE_XATTR))
			return NULL;
	}

	txattr_addr = calloc(inline_size + F2FS_BLKSIZE, 1);
	ASSERT(txattr_addr);

	if (inline_size)
		memcpy(txattr_addr, inline_xattr_addr(&inode->i), inline_size);

	/* Read from xattr node block. */
	if (xnid) {
		struct node_info ni;
		int ret;

		get_node_info(sbi, xnid, &ni);
		ret = dev_read_block(txattr_addr + inline_size, ni.blk_addr);
		ASSERT(ret >= 0);
		memset(txattr_addr + inline_size + F2FS_BLKSIZE -
				sizeof(struct node_footer), 0,
				sizeof(struct node_footer));
	}

	header = XATTR_HDR(txattr_addr);

	/* Never been allocated xattrs */
	if (le32_to_cpu(header->h_magic) != F2FS_XATTR_MAGIC) {
		header->h_magic = cpu_to_le32(F2FS_XATTR_MAGIC);
		header->h_refcount = cpu_to_le32(1);
	}
	return txattr_addr;
}

static struct f2fs_xattr_entry *__find_xattr(void *base_addr,
				void *last_base_addr, int index,
				size_t len, const char *name)
{
	struct f2fs_xattr_entry *entry;

	list_for_each_xattr(entry, base_addr) {
		if ((void *)(entry) + sizeof(__u32) > last_base_addr ||
			(void *)XATTR_NEXT_ENTRY(entry) > last_base_addr) {
			MSG(0, "xattr entry crosses the end of xattr space\n");
			return NULL;
		}

		if (entry->e_name_index != index)
			continue;
		if (entry->e_name_len != len)
			continue;
		if (!memcmp(entry->e_name, name, len))
			break;
	}
	return entry;
}

void write_all_xattrs(struct f2fs_sb_info *sbi,
		struct f2fs_node *inode, __u32 hsize, void *txattr_addr)
{
	void *xattr_addr;
	struct dnode_of_data dn;
	struct node_info ni;
	struct f2fs_node *xattr_node;
	nid_t new_nid = 0;
	block_t blkaddr;
	nid_t xnid = le32_to_cpu(inode->i.i_xattr_nid);
	u64 inline_size = inline_xattr_size(&inode->i);
	int ret;
	bool xattrblk_alloced = false;
	struct seg_entry *se;

	memcpy(inline_xattr_addr(&inode->i), txattr_addr, inline_size);

	if (hsize <= inline_size)
		return;

	if (!xnid) {
		f2fs_alloc_nid(sbi, &new_nid);

		set_new_dnode(&dn, inode, NULL, new_nid);
		/* NAT entry would be updated by new_node_page. */
		blkaddr = new_node_block(sbi, &dn, XATTR_NODE_OFFSET);
		ASSERT(dn.node_blk);
		xattr_node = dn.node_blk;
		inode->i.i_xattr_nid = cpu_to_le32(new_nid);
		xattrblk_alloced = true;
	} else {
		set_new_dnode(&dn, inode, NULL, xnid);
		get_node_info(sbi, xnid, &ni);
		blkaddr = ni.blk_addr;
		xattr_node = calloc(F2FS_BLKSIZE, 1);
		ASSERT(xattr_node);
		ret = dev_read_block(xattr_node, ni.blk_addr);
		if (ret < 0)
			goto free_xattr_node;
	}

	/* write to xattr node block */
	xattr_addr = (void *)xattr_node;
	memcpy(xattr_addr, txattr_addr + inline_size,
			F2FS_BLKSIZE - sizeof(struct node_footer));
	se = get_seg_entry(sbi, GET_SEGNO(sbi, blkaddr));
	ret = xattrblk_alloced ? dev_write_block(xattr_node, blkaddr,
					f2fs_io_type_to_rw_hint(se->type)) :
		update_block(sbi, xattr_node, &blkaddr, NULL);

free_xattr_node:
	free(xattr_node);
	ASSERT(ret >= 0);
}

int f2fs_setxattr(struct f2fs_sb_info *sbi, nid_t ino, int index, const char *name,
		const void *value, size_t size, int flags)
{
	struct f2fs_node *inode;
	void *base_addr;
	void *last_base_addr;
	struct f2fs_xattr_entry *here, *last;
	struct node_info ni;
	int error = 0;
	int len;
	int found, newsize;
	__u32 new_hsize;
	int ret;

	if (name == NULL)
		return -EINVAL;

	if (value == NULL)
		return -EINVAL;

	len = strlen(name);

	if (len > F2FS_NAME_LEN || size > MAX_VALUE_LEN)
		return -ERANGE;

	if (ino < 3)
		return -EINVAL;

	/* Now We just support selinux */
	ASSERT(index == F2FS_XATTR_INDEX_SECURITY);

	get_node_info(sbi, ino, &ni);
	inode = calloc(F2FS_BLKSIZE, 1);
	ASSERT(inode);
	ret = dev_read_block(inode, ni.blk_addr);
	ASSERT(ret >= 0);

	base_addr = read_all_xattrs(sbi, inode, true);
	ASSERT(base_addr);

	last_base_addr = (void *)base_addr + XATTR_SIZE(&inode->i);

	here = __find_xattr(base_addr, last_base_addr, index, len, name);
	if (!here) {
		MSG(0, "Need to run fsck due to corrupted xattr.\n");
		error = -EINVAL;
		goto exit;
	}

	found = IS_XATTR_LAST_ENTRY(here) ? 0 : 1;

	if ((flags & XATTR_REPLACE) && !found) {
		error = -ENODATA;
		goto exit;
	} else if ((flags & XATTR_CREATE) && found) {
		error = -EEXIST;
		goto exit;
	}

	last = here;
	while (!IS_XATTR_LAST_ENTRY(last))
		last = XATTR_NEXT_ENTRY(last);

	newsize = XATTR_ALIGN(sizeof(struct f2fs_xattr_entry) + len + size);

	/* 1. Check space */
	if (value) {
		int free;
		/*
		 * If value is NULL, it is remove operation.
		 * In case of update operation, we calculate free.
		 */
		free = MIN_OFFSET - ((char *)last - (char *)base_addr);
		if (found)
			free = free + ENTRY_SIZE(here);
		if (free < newsize) {
			error = -ENOSPC;
			goto exit;
		}
	}

	/* 2. Remove old entry */
	if (found) {
		/*
		 * If entry if sound, remove old entry.
		 * If not found, remove operation is not needed
		 */
		struct f2fs_xattr_entry *next = XATTR_NEXT_ENTRY(here);
		int oldsize = ENTRY_SIZE(here);

		memmove(here, next, (char *)last - (char *)next);
		last = (struct f2fs_xattr_entry *)((char *)last - oldsize);
		memset(last, 0, oldsize);

	}

	new_hsize = (char *)last - (char *)base_addr;

	/* 3. Write new entry */
	if (value) {
		char *pval;
		/*
		 * Before we come here, old entry is removed.
		 * We just write new entry.
		 */
		memset(last, 0, newsize);
		last->e_name_index = index;
		last->e_name_len = len;
		memcpy(last->e_name, name, len);
		pval = last->e_name + len;
		memcpy(pval, value, size);
		last->e_value_size = cpu_to_le16(size);
		new_hsize += newsize;
	}

	write_all_xattrs(sbi, inode, new_hsize, base_addr);

	/* inode need update */
	ASSERT(update_inode(sbi, inode, &ni.blk_addr) >= 0);
exit:
	free(inode);
	free(base_addr);
	return error;
}

int inode_set_selinux(struct f2fs_sb_info *sbi, u32 ino, const char *secon)
{
	if (!secon)
		return 0;

	return f2fs_setxattr(sbi, ino, F2FS_XATTR_INDEX_SECURITY,
			XATTR_SELINUX_SUFFIX, secon, strlen(secon), 1);
}
