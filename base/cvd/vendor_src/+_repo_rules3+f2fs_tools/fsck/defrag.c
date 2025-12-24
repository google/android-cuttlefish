/**
 * defrag.c
 *
 * Copyright (c) 2015 Jaegeuk Kim <jaegeuk@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "fsck.h"

static int migrate_block(struct f2fs_sb_info *sbi, u64 from, u64 to)
{
	void *raw = calloc(F2FS_BLKSIZE, 1);
	struct seg_entry *se;
	struct f2fs_summary sum;
	u64 offset;
	int ret, type;

	ASSERT(raw != NULL);

	/* read from */
	ret = dev_read_block(raw, from);
	ASSERT(ret >= 0);

	/* get segment type */
	se = get_seg_entry(sbi, GET_SEGNO(sbi, from));
	/* write to */
	ret = dev_write_block(raw, to, f2fs_io_type_to_rw_hint(se->type));
	ASSERT(ret >= 0);

	/* update sit bitmap & valid_blocks && se->type */
	offset = OFFSET_IN_SEG(sbi, from);
	type = se->type;
	se->valid_blocks--;
	f2fs_clear_bit(offset, (char *)se->cur_valid_map);
	se->dirty = 1;

	se = get_seg_entry(sbi, GET_SEGNO(sbi, to));
	offset = OFFSET_IN_SEG(sbi, to);
	se->type = type;
	se->valid_blocks++;
	f2fs_set_bit(offset, (char *)se->cur_valid_map);
	se->dirty = 1;

	/* read/write SSA */
	get_sum_entry(sbi, from, &sum);
	update_sum_entry(sbi, to, &sum);

	/* if data block, read node and update node block */
	if (IS_DATASEG(type))
		update_data_blkaddr(sbi, le32_to_cpu(sum.nid),
				le16_to_cpu(sum.ofs_in_node), to, NULL);
	else
		update_nat_blkaddr(sbi, 0, le32_to_cpu(sum.nid), to);

	DBG(1, "Migrate %s block %"PRIx64" -> %"PRIx64"\n",
					IS_DATASEG(type) ? "data" : "node",
					from, to);
	free(raw);
	return 0;
}

int f2fs_defragment(struct f2fs_sb_info *sbi, u64 from, u64 len, u64 to, int left)
{
	struct seg_entry *se;
	u64 idx, offset;

	/* flush NAT/SIT journal entries */
	flush_journal_entries(sbi);

	for (idx = from; idx < from + len; idx++) {
		u64 target = to;

		se = get_seg_entry(sbi, GET_SEGNO(sbi, idx));
		offset = OFFSET_IN_SEG(sbi, idx);

		if (!f2fs_test_bit(offset, (const char *)se->cur_valid_map))
			continue;

		if (find_next_free_block(sbi, &target, left, se->type, false)) {
			MSG(0, "Not enough space to migrate blocks");
			return -1;
		}

		if (migrate_block(sbi, idx, target)) {
			ASSERT_MSG("Found inconsistency: please run FSCK");
			return -1;
		}
	}

	/* update curseg info; can update sit->types */
	move_curseg_info(sbi, to, left);
	zero_journal_entries(sbi);
	write_curseg_info(sbi);

	/* flush dirty sit entries */
	flush_sit_entries(sbi);

	write_checkpoint(sbi);

	return 0;
}
