/**
 * resize.c
 *
 * Copyright (c) 2015 Jaegeuk Kim <jaegeuk@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "fsck.h"

static int get_new_sb(struct f2fs_super_block *sb)
{
	uint32_t zone_size_bytes;
	uint64_t zone_align_start_offset;
	uint32_t blocks_for_sit, blocks_for_nat, blocks_for_ssa;
	uint32_t sit_segments, nat_segments, diff, total_meta_segments;
	uint32_t total_valid_blks_available;
	uint32_t sit_bitmap_size, max_sit_bitmap_size;
	uint32_t max_nat_bitmap_size, max_nat_segments;
	uint32_t segment_size_bytes = 1 << (get_sb(log_blocksize) +
					get_sb(log_blocks_per_seg));
	uint32_t blks_per_seg = 1 << get_sb(log_blocks_per_seg);
	uint32_t segs_per_zone = get_sb(segs_per_sec) * get_sb(secs_per_zone);

	set_sb(block_count, c.target_sectors >>
				get_sb(log_sectors_per_block));

	zone_size_bytes = segment_size_bytes * segs_per_zone;
	zone_align_start_offset =
		((uint64_t) c.start_sector * DEFAULT_SECTOR_SIZE +
		2 * F2FS_BLKSIZE + zone_size_bytes - 1) /
		zone_size_bytes * zone_size_bytes -
		(uint64_t) c.start_sector * DEFAULT_SECTOR_SIZE;

	set_sb(segment_count, (c.target_sectors * c.sector_size -
				zone_align_start_offset) / segment_size_bytes /
				c.segs_per_sec * c.segs_per_sec);

	if (c.safe_resize)
		goto safe_resize;

	blocks_for_sit = SIZE_ALIGN(get_sb(segment_count), SIT_ENTRY_PER_BLOCK);
	sit_segments = SEG_ALIGN(blocks_for_sit);
	set_sb(segment_count_sit, sit_segments * 2);
	set_sb(nat_blkaddr, get_sb(sit_blkaddr) +
				get_sb(segment_count_sit) * blks_per_seg);

	total_valid_blks_available = (get_sb(segment_count) -
			(get_sb(segment_count_ckpt) +
			get_sb(segment_count_sit))) * blks_per_seg;
	blocks_for_nat = SIZE_ALIGN(total_valid_blks_available,
					NAT_ENTRY_PER_BLOCK);

	if (c.large_nat_bitmap) {
		nat_segments = SEG_ALIGN(blocks_for_nat) *
						DEFAULT_NAT_ENTRY_RATIO / 100;
		set_sb(segment_count_nat, nat_segments ? nat_segments : 1);

		max_nat_bitmap_size = (get_sb(segment_count_nat) <<
						get_sb(log_blocks_per_seg)) / 8;
		set_sb(segment_count_nat, get_sb(segment_count_nat) * 2);
	} else {
		set_sb(segment_count_nat, SEG_ALIGN(blocks_for_nat));
		max_nat_bitmap_size = 0;
	}

	sit_bitmap_size = ((get_sb(segment_count_sit) / 2) <<
				get_sb(log_blocks_per_seg)) / 8;
	if (sit_bitmap_size > MAX_SIT_BITMAP_SIZE)
		max_sit_bitmap_size = MAX_SIT_BITMAP_SIZE;
	else
		max_sit_bitmap_size = sit_bitmap_size;

	if (c.large_nat_bitmap) {
		/* use cp_payload if free space of f2fs_checkpoint is not enough */
		if (max_sit_bitmap_size + max_nat_bitmap_size >
						MAX_BITMAP_SIZE_IN_CKPT) {
			uint32_t diff =  max_sit_bitmap_size +
						max_nat_bitmap_size -
						MAX_BITMAP_SIZE_IN_CKPT;
			set_sb(cp_payload, F2FS_BLK_ALIGN(diff));
		} else {
			set_sb(cp_payload, 0);
		}
	} else {
		/*
		 * It should be reserved minimum 1 segment for nat.
		 * When sit is too large, we should expand cp area.
		 * It requires more pages for cp.
		 */
		if (max_sit_bitmap_size > MAX_SIT_BITMAP_SIZE_IN_CKPT) {
			max_nat_bitmap_size = MAX_BITMAP_SIZE_IN_CKPT;
			set_sb(cp_payload, F2FS_BLK_ALIGN(max_sit_bitmap_size));
		} else {
			max_nat_bitmap_size = MAX_BITMAP_SIZE_IN_CKPT -
							max_sit_bitmap_size;
			set_sb(cp_payload, 0);
		}

		max_nat_segments = (max_nat_bitmap_size * 8) >>
					get_sb(log_blocks_per_seg);

		if (get_sb(segment_count_nat) > max_nat_segments)
			set_sb(segment_count_nat, max_nat_segments);

		set_sb(segment_count_nat, get_sb(segment_count_nat) * 2);
	}

	set_sb(ssa_blkaddr, get_sb(nat_blkaddr) +
				get_sb(segment_count_nat) * blks_per_seg);

	total_valid_blks_available = (get_sb(segment_count) -
			(get_sb(segment_count_ckpt) +
			get_sb(segment_count_sit) +
			get_sb(segment_count_nat))) * blks_per_seg;

	blocks_for_ssa = total_valid_blks_available / blks_per_seg + 1;

	set_sb(segment_count_ssa, SEG_ALIGN(blocks_for_ssa));

	total_meta_segments = get_sb(segment_count_ckpt) +
		get_sb(segment_count_sit) +
		get_sb(segment_count_nat) +
		get_sb(segment_count_ssa);

	diff = total_meta_segments % segs_per_zone;
	if (diff)
		set_sb(segment_count_ssa, get_sb(segment_count_ssa) +
			(segs_per_zone - diff));

	set_sb(main_blkaddr, get_sb(ssa_blkaddr) + get_sb(segment_count_ssa) *
			 blks_per_seg);

safe_resize:
	set_sb(segment_count_main, get_sb(segment_count) -
			(get_sb(segment_count_ckpt) +
			 get_sb(segment_count_sit) +
			 get_sb(segment_count_nat) +
			 get_sb(segment_count_ssa)));

	set_sb(section_count, get_sb(segment_count_main) /
						get_sb(segs_per_sec));

	set_sb(segment_count_main, get_sb(section_count) *
						get_sb(segs_per_sec));

	/* Let's determine the best reserved and overprovisioned space */
	if (c.new_overprovision == 0)
		c.new_overprovision = get_best_overprovision(sb);

	c.new_reserved_segments =
		(100 / c.new_overprovision + 1 + NR_CURSEG_TYPE) *
						get_sb(segs_per_sec);

	if ((get_sb(segment_count_main) - 2) < c.new_reserved_segments ||
		get_sb(segment_count_main) * blks_per_seg >
						get_sb(block_count)) {
		MSG(0, "\tError: Device size is not sufficient for F2FS volume, "
			"more segment needed =%u",
			c.new_reserved_segments -
			(get_sb(segment_count_main) - 2));
		return -1;
	}
	return 0;
}

static void migrate_main(struct f2fs_sb_info *sbi, unsigned int offset)
{
	void *raw = calloc(F2FS_BLKSIZE, 1);
	struct seg_entry *se;
	block_t from, to;
	int i, j, ret;
	struct f2fs_summary sum;

	ASSERT(raw != NULL);

	for (i = MAIN_SEGS(sbi) - 1; i >= 0; i--) {
		se = get_seg_entry(sbi, i);
		if (!se->valid_blocks)
			continue;

		for (j = sbi->blocks_per_seg - 1; j >= 0; j--) {
			if (!f2fs_test_bit(j, (const char *)se->cur_valid_map))
				continue;

			from = START_BLOCK(sbi, i) + j;
			ret = dev_read_block(raw, from);
			ASSERT(ret >= 0);

			to = from + offset;
			ret = dev_write_block(raw, to,
					      f2fs_io_type_to_rw_hint(se->type));
			ASSERT(ret >= 0);

			get_sum_entry(sbi, from, &sum);

			if (IS_DATASEG(se->type))
				update_data_blkaddr(sbi, le32_to_cpu(sum.nid),
					le16_to_cpu(sum.ofs_in_node), to, NULL);
			else
				update_nat_blkaddr(sbi, 0,
						le32_to_cpu(sum.nid), to);
		}
	}
	free(raw);
	DBG(0, "Info: Done to migrate Main area: main_blkaddr = 0x%x -> 0x%x\n",
				START_BLOCK(sbi, 0),
				START_BLOCK(sbi, 0) + offset);
}

static void move_ssa(struct f2fs_sb_info *sbi, unsigned int segno,
					block_t new_sum_blk_addr)
{
	struct f2fs_summary_block *sum_blk;
	int type;

	sum_blk = get_sum_block(sbi, segno, &type);
	if (type < SEG_TYPE_MAX) {
		int ret;

		ret = dev_write_block(sum_blk, new_sum_blk_addr,
				      WRITE_LIFE_NONE);
		ASSERT(ret >= 0);
		DBG(1, "Write summary block: (%d) segno=%x/%x --> (%d) %x\n",
				type, segno, GET_SUM_BLKADDR(sbi, segno),
				IS_SUM_NODE_SEG(sum_blk),
				new_sum_blk_addr);
	}
	if (type == SEG_TYPE_NODE || type == SEG_TYPE_DATA ||
			type == SEG_TYPE_MAX) {
		free(sum_blk);
	}
	DBG(1, "Info: Done to migrate SSA blocks\n");
}

static void migrate_ssa(struct f2fs_sb_info *sbi,
		struct f2fs_super_block *new_sb, unsigned int offset)
{
	struct f2fs_super_block *sb = F2FS_RAW_SUPER(sbi);
	block_t old_sum_blkaddr = get_sb(ssa_blkaddr);
	block_t new_sum_blkaddr = get_newsb(ssa_blkaddr);
	block_t end_sum_blkaddr = get_newsb(main_blkaddr);
	block_t expand_sum_blkaddr = new_sum_blkaddr +
					MAIN_SEGS(sbi) - offset;
	block_t blkaddr;
	int ret;
	void *zero_block = calloc(F2FS_BLKSIZE, 1);
	ASSERT(zero_block);

	if (offset && new_sum_blkaddr < old_sum_blkaddr + offset) {
		blkaddr = new_sum_blkaddr;
		while (blkaddr < end_sum_blkaddr) {
			if (blkaddr < expand_sum_blkaddr) {
				move_ssa(sbi, offset++, blkaddr++);
			} else {
				ret = dev_write_block(zero_block, blkaddr++,
						      WRITE_LIFE_NONE);
				ASSERT(ret >=0);
			}
		}
	} else {
		blkaddr = end_sum_blkaddr - 1;
		offset = MAIN_SEGS(sbi) - 1;
		while (blkaddr >= new_sum_blkaddr) {
			if (blkaddr >= expand_sum_blkaddr) {
				ret = dev_write_block(zero_block, blkaddr--,
						      WRITE_LIFE_NONE);
				ASSERT(ret >=0);
			} else {
				move_ssa(sbi, offset--, blkaddr--);
			}
		}
	}

	DBG(0, "Info: Done to migrate SSA blocks: sum_blkaddr = 0x%x -> 0x%x\n",
				old_sum_blkaddr, new_sum_blkaddr);
	free(zero_block);
}

static int shrink_nats(struct f2fs_sb_info *sbi,
				struct f2fs_super_block *new_sb)
{
	struct f2fs_super_block *sb = F2FS_RAW_SUPER(sbi);
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	block_t old_nat_blkaddr = get_sb(nat_blkaddr);
	unsigned int nat_blocks;
	void *nat_block, *zero_block;
	int nid, ret, new_max_nid;
	pgoff_t block_off;
	pgoff_t block_addr;
	int seg_off;

	nat_block = malloc(F2FS_BLKSIZE);
	ASSERT(nat_block);
	zero_block = calloc(F2FS_BLKSIZE, 1);
	ASSERT(zero_block);

	nat_blocks = get_newsb(segment_count_nat) >> 1;
	nat_blocks = nat_blocks << get_sb(log_blocks_per_seg);
	new_max_nid = NAT_ENTRY_PER_BLOCK * nat_blocks;

	for (nid = nm_i->max_nid - 1; nid > new_max_nid; nid -= NAT_ENTRY_PER_BLOCK) {
		block_off = nid / NAT_ENTRY_PER_BLOCK;
		seg_off = block_off >> sbi->log_blocks_per_seg;
		block_addr = (pgoff_t)(old_nat_blkaddr +
				(seg_off << sbi->log_blocks_per_seg << 1) +
				(block_off & ((1 << sbi->log_blocks_per_seg) - 1)));

		if (f2fs_test_bit(block_off, nm_i->nat_bitmap))
			block_addr += sbi->blocks_per_seg;

		ret = dev_read_block(nat_block, block_addr);
		ASSERT(ret >= 0);

		if (memcmp(zero_block, nat_block, F2FS_BLKSIZE)) {
			ret = -1;
			goto not_avail;
		}
	}
	ret = 0;
	nm_i->max_nid = new_max_nid;
not_avail:
	free(nat_block);
	free(zero_block);
	return ret;
}

static void migrate_nat(struct f2fs_sb_info *sbi,
			struct f2fs_super_block *new_sb)
{
	struct f2fs_super_block *sb = F2FS_RAW_SUPER(sbi);
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	block_t old_nat_blkaddr = get_sb(nat_blkaddr);
	block_t new_nat_blkaddr = get_newsb(nat_blkaddr);
	unsigned int nat_blocks;
	void *nat_block;
	int nid, ret, new_max_nid;
	pgoff_t block_off;
	pgoff_t block_addr;
	int seg_off;

	nat_block = malloc(F2FS_BLKSIZE);
	ASSERT(nat_block);

	for (nid = nm_i->max_nid - 1; nid >= 0; nid -= NAT_ENTRY_PER_BLOCK) {
		block_off = nid / NAT_ENTRY_PER_BLOCK;
		seg_off = block_off >> sbi->log_blocks_per_seg;
		block_addr = (pgoff_t)(old_nat_blkaddr +
				(seg_off << sbi->log_blocks_per_seg << 1) +
				(block_off & ((1 << sbi->log_blocks_per_seg) - 1)));

		/* move to set #0 */
		if (f2fs_test_bit(block_off, nm_i->nat_bitmap)) {
			block_addr += sbi->blocks_per_seg;
			f2fs_clear_bit(block_off, nm_i->nat_bitmap);
		}

		ret = dev_read_block(nat_block, block_addr);
		ASSERT(ret >= 0);

		block_addr = (pgoff_t)(new_nat_blkaddr +
				(seg_off << sbi->log_blocks_per_seg << 1) +
				(block_off & ((1 << sbi->log_blocks_per_seg) - 1)));

		/* new bitmap should be zeros */
		ret = dev_write_block(nat_block, block_addr, WRITE_LIFE_NONE);
		ASSERT(ret >= 0);
	}
	/* zero out newly assigned nids */
	memset(nat_block, 0, F2FS_BLKSIZE);
	nat_blocks = get_newsb(segment_count_nat) >> 1;
	nat_blocks = nat_blocks << get_sb(log_blocks_per_seg);
	new_max_nid = NAT_ENTRY_PER_BLOCK * nat_blocks;

	DBG(1, "Write NAT block: %x->%x, max_nid=%x->%x\n",
			old_nat_blkaddr, new_nat_blkaddr,
			get_sb(segment_count_nat),
			get_newsb(segment_count_nat));

	for (nid = nm_i->max_nid; nid < new_max_nid;
				nid += NAT_ENTRY_PER_BLOCK) {
		block_off = nid / NAT_ENTRY_PER_BLOCK;
		seg_off = block_off >> sbi->log_blocks_per_seg;
		block_addr = (pgoff_t)(new_nat_blkaddr +
				(seg_off << sbi->log_blocks_per_seg << 1) +
				(block_off & ((1 << sbi->log_blocks_per_seg) - 1)));
		ret = dev_write_block(nat_block, block_addr, WRITE_LIFE_NONE);
		ASSERT(ret >= 0);
		DBG(3, "Write NAT: %lx\n", block_addr);
	}
	free(nat_block);
	DBG(0, "Info: Done to migrate NAT blocks: nat_blkaddr = 0x%x -> 0x%x\n",
			old_nat_blkaddr, new_nat_blkaddr);
}

static void migrate_sit(struct f2fs_sb_info *sbi,
		struct f2fs_super_block *new_sb, unsigned int offset)
{
	struct sit_info *sit_i = SIT_I(sbi);
	unsigned int ofs = 0, pre_ofs = 0;
	unsigned int segno, index;
	struct f2fs_sit_block *sit_blk = calloc(F2FS_BLKSIZE, 1);
	block_t sit_blks = get_newsb(segment_count_sit) <<
						(sbi->log_blocks_per_seg - 1);
	struct seg_entry *se;
	block_t blk_addr = 0;
	int ret;

	ASSERT(sit_blk);

	/* initialize with zeros */
	for (index = 0; index < sit_blks; index++) {
		ret = dev_write_block(sit_blk, get_newsb(sit_blkaddr) + index,
				      WRITE_LIFE_NONE);
		ASSERT(ret >= 0);
		DBG(3, "Write zero sit: %x\n", get_newsb(sit_blkaddr) + index);
	}

	for (segno = 0; segno < MAIN_SEGS(sbi); segno++) {
		struct f2fs_sit_entry *sit;

		se = get_seg_entry(sbi, segno);
		if (segno < offset) {
			ASSERT(se->valid_blocks == 0);
			continue;
		}

		ofs = SIT_BLOCK_OFFSET(sit_i, segno - offset);

		if (ofs != pre_ofs) {
			blk_addr = get_newsb(sit_blkaddr) + pre_ofs;
			ret = dev_write_block(sit_blk, blk_addr,
					      WRITE_LIFE_NONE);
			ASSERT(ret >= 0);
			DBG(1, "Write valid sit: %x\n", blk_addr);

			pre_ofs = ofs;
			memset(sit_blk, 0, F2FS_BLKSIZE);
		}

		sit = &sit_blk->entries[SIT_ENTRY_OFFSET(sit_i, segno - offset)];
		memcpy(sit->valid_map, se->cur_valid_map, SIT_VBLOCK_MAP_SIZE);
		sit->vblocks = cpu_to_le16((se->type << SIT_VBLOCKS_SHIFT) |
							se->valid_blocks);
	}
	blk_addr = get_newsb(sit_blkaddr) + ofs;
	ret = dev_write_block(sit_blk, blk_addr, WRITE_LIFE_NONE);
	DBG(1, "Write valid sit: %x\n", blk_addr);
	ASSERT(ret >= 0);

	free(sit_blk);
	DBG(0, "Info: Done to restore new SIT blocks: 0x%x\n",
					get_newsb(sit_blkaddr));
}

static void rebuild_checkpoint(struct f2fs_sb_info *sbi,
			struct f2fs_super_block *new_sb, unsigned int offset)
{
	struct f2fs_checkpoint *cp = F2FS_CKPT(sbi);
	unsigned long long cp_ver = get_cp(checkpoint_ver);
	struct f2fs_checkpoint *new_cp;
	struct f2fs_super_block *sb = F2FS_RAW_SUPER(sbi);
	unsigned int free_segment_count, new_segment_count;
	block_t new_cp_blks = 1 + get_newsb(cp_payload);
	block_t orphan_blks = 0;
	block_t new_cp_blk_no, old_cp_blk_no;
	uint32_t crc = 0;
	u32 flags;
	void *buf;
	int i, ret;

	new_cp = calloc(new_cp_blks * F2FS_BLKSIZE, 1);
	ASSERT(new_cp);

	buf = malloc(F2FS_BLKSIZE);
	ASSERT(buf);

	/* ovp / free segments */
	set_cp(rsvd_segment_count, c.new_reserved_segments);
	set_cp(overprov_segment_count, (get_newsb(segment_count_main) -
			get_cp(rsvd_segment_count)) *
			c.new_overprovision / 100);

	/* give 2 sections (DATA and NODE) to trigger GC in advance */
	if (get_cp(overprov_segment_count) < get_cp(rsvd_segment_count))
		set_cp(overprov_segment_count, get_cp(rsvd_segment_count));

	set_cp(overprov_segment_count, get_cp(overprov_segment_count) +
						2 * get_sb(segs_per_sec));

	DBG(0, "Info: Overprovision ratio = %.3lf%%\n", c.new_overprovision);
	DBG(0, "Info: Overprovision segments = %u (GC reserved = %u)\n",
					get_cp(overprov_segment_count),
					c.new_reserved_segments);

	free_segment_count = get_free_segments(sbi);
	new_segment_count = get_newsb(segment_count_main) -
					get_sb(segment_count_main);

	set_cp(free_segment_count, free_segment_count + new_segment_count);
	set_cp(user_block_count, ((get_newsb(segment_count_main) -
			get_cp(overprov_segment_count)) * c.blks_per_seg));

	if (is_set_ckpt_flags(cp, CP_ORPHAN_PRESENT_FLAG))
		orphan_blks = __start_sum_addr(sbi) - 1;

	set_cp(cp_pack_start_sum, 1 + get_newsb(cp_payload));
	set_cp(cp_pack_total_block_count, 8 + orphan_blks + get_newsb(cp_payload));

	/* cur->segno - offset */
	for (i = 0; i < NO_CHECK_TYPE; i++) {
		if (i < CURSEG_HOT_NODE) {
			set_cp(cur_data_segno[i],
					CURSEG_I(sbi, i)->segno - offset);
		} else {
			int n = i - CURSEG_HOT_NODE;

			set_cp(cur_node_segno[n],
					CURSEG_I(sbi, i)->segno - offset);
		}
	}

	/* sit / nat ver bitmap bytesize */
	set_cp(sit_ver_bitmap_bytesize,
			((get_newsb(segment_count_sit) / 2) <<
			get_newsb(log_blocks_per_seg)) / 8);
	set_cp(nat_ver_bitmap_bytesize,
			((get_newsb(segment_count_nat) / 2) <<
			get_newsb(log_blocks_per_seg)) / 8);

	/* update nat_bits flag */
	flags = update_nat_bits_flags(new_sb, cp, get_cp(ckpt_flags));
	if (flags & CP_COMPACT_SUM_FLAG)
		flags &= ~CP_COMPACT_SUM_FLAG;
	if (flags & CP_LARGE_NAT_BITMAP_FLAG)
		set_cp(checksum_offset, CP_MIN_CHKSUM_OFFSET);
	else
		set_cp(checksum_offset, CP_CHKSUM_OFFSET);

	set_cp(ckpt_flags, flags);

	memcpy(new_cp, cp, (unsigned char *)cp->sit_nat_version_bitmap -
						(unsigned char *)cp);
	if (c.safe_resize)
		memcpy((void *)new_cp + CP_BITMAP_OFFSET,
			(void *)cp + CP_BITMAP_OFFSET,
			F2FS_BLKSIZE - CP_BITMAP_OFFSET);

	new_cp->checkpoint_ver = cpu_to_le64(cp_ver + 1);

	crc = f2fs_checkpoint_chksum(new_cp);
	*((__le32 *)((unsigned char *)new_cp + get_cp(checksum_offset))) =
							cpu_to_le32(crc);

	/* Write a new checkpoint in the other set */
	new_cp_blk_no = old_cp_blk_no = get_sb(cp_blkaddr);
	if (sbi->cur_cp == 2)
		old_cp_blk_no += 1 << get_sb(log_blocks_per_seg);
	else
		new_cp_blk_no += 1 << get_sb(log_blocks_per_seg);

	/* write first cp */
	ret = dev_write_block(new_cp, new_cp_blk_no++, WRITE_LIFE_NONE);
	ASSERT(ret >= 0);

	memset(buf, 0, F2FS_BLKSIZE);
	for (i = 0; i < get_newsb(cp_payload); i++) {
		ret = dev_write_block(buf, new_cp_blk_no++, WRITE_LIFE_NONE);
		ASSERT(ret >= 0);
	}

	for (i = 0; i < orphan_blks; i++) {
		block_t orphan_blk_no = old_cp_blk_no + 1 + get_sb(cp_payload);

		ret = dev_read_block(buf, orphan_blk_no++);
		ASSERT(ret >= 0);

		ret = dev_write_block(buf, new_cp_blk_no++, WRITE_LIFE_NONE);
		ASSERT(ret >= 0);
	}

	/* update summary blocks having nullified journal entries */
	for (i = 0; i < NO_CHECK_TYPE; i++) {
		struct curseg_info *curseg = CURSEG_I(sbi, i);

		ret = dev_write_block(curseg->sum_blk, new_cp_blk_no++,
				      WRITE_LIFE_NONE);
		ASSERT(ret >= 0);
	}

	/* write the last cp */
	ret = dev_write_block(new_cp, new_cp_blk_no++, WRITE_LIFE_NONE);
	ASSERT(ret >= 0);

	/* Write nat bits */
	if (flags & CP_NAT_BITS_FLAG)
		write_nat_bits(sbi, new_sb, new_cp, sbi->cur_cp == 1 ? 2 : 1);

	/* disable old checkpoint */
	memset(buf, 0, F2FS_BLKSIZE);
	ret = dev_write_block(buf, old_cp_blk_no, WRITE_LIFE_NONE);
	ASSERT(ret >= 0);

	free(buf);
	free(new_cp);
	DBG(0, "Info: Done to rebuild checkpoint blocks\n");
}

static int f2fs_resize_check(struct f2fs_sb_info *sbi, struct f2fs_super_block *new_sb)
{
	struct f2fs_checkpoint *cp = F2FS_CKPT(sbi);
	block_t user_block_count;
	unsigned int overprov_segment_count;

	overprov_segment_count = (get_newsb(segment_count_main) -
			c.new_reserved_segments) *
			c.new_overprovision / 100;

	overprov_segment_count += 2 * get_newsb(segs_per_sec);

	user_block_count = (get_newsb(segment_count_main) -
			overprov_segment_count) * c.blks_per_seg;

	if (get_cp(valid_block_count) > user_block_count)
		return -1;

	return 0;
}

static int f2fs_resize_grow(struct f2fs_sb_info *sbi)
{
	struct f2fs_super_block *sb = F2FS_RAW_SUPER(sbi);
	struct f2fs_super_block new_sb_raw;
	struct f2fs_super_block *new_sb = &new_sb_raw;
	block_t end_blkaddr, old_main_blkaddr, new_main_blkaddr;
	unsigned int offset;
	unsigned int offset_seg = 0;
	int err = -1;

	/* flush NAT/SIT journal entries */
	flush_journal_entries(sbi);

	memcpy(new_sb, F2FS_RAW_SUPER(sbi), sizeof(*new_sb));
	if (get_new_sb(new_sb))
		return -1;

	if (f2fs_resize_check(sbi, new_sb) < 0)
		return -1;

	/* check nat availability */
	if (get_sb(segment_count_nat) > get_newsb(segment_count_nat)) {
		err = shrink_nats(sbi, new_sb);
		if (err) {
			MSG(0, "\tError: Failed to shrink NATs\n");
			return err;
		}
	}

	old_main_blkaddr = get_sb(main_blkaddr);
	new_main_blkaddr = get_newsb(main_blkaddr);
	offset = new_main_blkaddr - old_main_blkaddr;
	end_blkaddr = (get_sb(segment_count_main) <<
			get_sb(log_blocks_per_seg)) + get_sb(main_blkaddr);

	err = -EAGAIN;
	if (new_main_blkaddr < end_blkaddr) {
		err = f2fs_defragment(sbi, old_main_blkaddr, offset,
						new_main_blkaddr, 0);
		if (!err)
			offset_seg = offset >> get_sb(log_blocks_per_seg);
		MSG(0, "Try to do defragement: %s\n", err ? "Skip": "Done");
	}
	/* move whole data region */
	if (err)
		migrate_main(sbi, offset);

	migrate_ssa(sbi, new_sb, offset_seg);
	migrate_nat(sbi, new_sb);
	migrate_sit(sbi, new_sb, offset_seg);
	rebuild_checkpoint(sbi, new_sb, offset_seg);
	update_superblock(new_sb, SB_MASK_ALL);
	print_raw_sb_info(sb);
	print_raw_sb_info(new_sb);

	return 0;
}

static int f2fs_resize_shrink(struct f2fs_sb_info *sbi)
{
	struct f2fs_super_block *sb = F2FS_RAW_SUPER(sbi);
	struct f2fs_super_block new_sb_raw;
	struct f2fs_super_block *new_sb = &new_sb_raw;
	block_t old_end_blkaddr, old_main_blkaddr;
	block_t new_end_blkaddr, new_main_blkaddr, tmp_end_blkaddr;
	unsigned int offset;
	int err = -1;

	/* flush NAT/SIT journal entries */
	flush_journal_entries(sbi);

	memcpy(new_sb, F2FS_RAW_SUPER(sbi), sizeof(*new_sb));
	if (get_new_sb(new_sb))
		return -1;

	if (f2fs_resize_check(sbi, new_sb) < 0)
		return -1;

	/* check nat availability */
	if (get_sb(segment_count_nat) > get_newsb(segment_count_nat)) {
		err = shrink_nats(sbi, new_sb);
		if (err) {
			MSG(0, "\tError: Failed to shrink NATs\n");
			return err;
		}
	}

	old_main_blkaddr = get_sb(main_blkaddr);
	new_main_blkaddr = get_newsb(main_blkaddr);
	offset = old_main_blkaddr - new_main_blkaddr;
	old_end_blkaddr = (get_sb(segment_count_main) <<
			get_sb(log_blocks_per_seg)) + get_sb(main_blkaddr);
	new_end_blkaddr = (get_newsb(segment_count_main) <<
			get_newsb(log_blocks_per_seg)) + get_newsb(main_blkaddr);

	tmp_end_blkaddr = new_end_blkaddr + offset;
	err = f2fs_defragment(sbi, tmp_end_blkaddr,
				old_end_blkaddr - tmp_end_blkaddr,
				tmp_end_blkaddr, 1);
	MSG(0, "Try to do defragement: %s\n", err ? "Insufficient Space": "Done");

	if (err) {
		return -ENOSPC;
	}

	update_superblock(new_sb, SB_MASK_ALL);
	rebuild_checkpoint(sbi, new_sb, 0);
	/*if (!c.safe_resize) {
		migrate_sit(sbi, new_sb, offset_seg);
		migrate_nat(sbi, new_sb);
		migrate_ssa(sbi, new_sb, offset_seg);
	}*/

	/* move whole data region */
	//if (err)
	//	migrate_main(sbi, offset);
	print_raw_sb_info(sb);
	print_raw_sb_info(new_sb);

	return 0;
}

int f2fs_resize(struct f2fs_sb_info *sbi)
{
	struct f2fs_super_block *sb = F2FS_RAW_SUPER(sbi);

	/* may different sector size */
	if ((c.target_sectors * c.sector_size >>
			get_sb(log_blocksize)) < get_sb(block_count)) {
		if (!c.safe_resize) {
			ASSERT_MSG("Nothing to resize, now only supports resizing with safe resize flag\n");
			return -1;
		} else {
			return f2fs_resize_shrink(sbi);
		}
	} else if (((c.target_sectors * c.sector_size >>
			get_sb(log_blocksize)) > get_sb(block_count)) ||
			c.ignore_error) {
		if (c.safe_resize) {
			ASSERT_MSG("Expanding resize doesn't support safe resize flag");
			return -1;
		}
		return f2fs_resize_grow(sbi);
	} else {
		MSG(0, "Nothing to resize.\n");
		return 0;
	}
}
