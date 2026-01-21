/**
 * f2fs_format.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * Dual licensed under the GPL or LGPL version 2 licenses.
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <f2fs_fs.h>
#include <assert.h>
#include <stdbool.h>

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif
#include <time.h>

#ifdef HAVE_UUID_UUID_H
#include <uuid/uuid.h>
#endif
#ifndef HAVE_LIBUUID
#define uuid_parse(a, b) -1
#define uuid_generate(a)
#define uuid_unparse(a, b) -1
#endif

#include "quota.h"
#include "f2fs_format_utils.h"

extern struct f2fs_configuration c;
struct f2fs_super_block raw_sb;
struct f2fs_super_block *sb = &raw_sb;
struct f2fs_checkpoint *cp;

static inline bool device_is_aliased(unsigned int dev_num)
{
	if (dev_num >= c.ndevs)
		return false;
	return c.devices[dev_num].alias_filename != NULL;
}

static inline unsigned int target_device_index(uint64_t blkaddr)
{
	int i;

	for (i = 0; i < c.ndevs; i++)
		if (c.devices[i].start_blkaddr <= blkaddr &&
				c.devices[i].end_blkaddr >= blkaddr)
			return i;
	return 0;
}

#define GET_SEGNO(blk_addr) ((blk_addr - get_sb(main_blkaddr)) / \
				c.blks_per_seg)
#define START_BLOCK(segno) (segno * c.blks_per_seg + get_sb(main_blkaddr))

/* Return first segment number of each area */
static inline uint32_t next_zone(int seg_type)
{
	uint32_t next_seg = c.cur_seg[seg_type] + c.segs_per_zone;
	uint64_t next_blkaddr = START_BLOCK(next_seg);
	int dev_num;

	dev_num = target_device_index(next_blkaddr);
	if (!device_is_aliased(dev_num))
		return GET_SEGNO(next_blkaddr);

	while (dev_num < c.ndevs && device_is_aliased(dev_num))
		dev_num++;

	return GET_SEGNO(c.devices[dev_num - 1].end_blkaddr + 1);
}

static inline uint32_t last_zone(uint32_t total_zone)
{
	uint32_t last_seg = (total_zone - 1) * c.segs_per_zone;
	uint64_t last_blkaddr = START_BLOCK(last_seg);
	int dev_num;

	dev_num = target_device_index(last_blkaddr);
	if (!device_is_aliased(dev_num))
		return GET_SEGNO(last_blkaddr);

	while (dev_num > 0 && device_is_aliased(dev_num))
		dev_num--;

	return GET_SEGNO(c.devices[dev_num + 1].start_blkaddr) -
		c.segs_per_zone;
}

#define last_section(cur)	(cur + (c.secs_per_zone - 1) * c.segs_per_sec)

/* Return time fixed by the user or current time by default */
#define mkfs_time ((c.fixed_time == -1) ? time(NULL) : c.fixed_time)

const char *media_ext_lists[] = {
	/* common prefix */
	"mp", // Covers mp3, mp4, mpeg, mpg
	"wm", // Covers wma, wmb, wmv
	"og", // Covers oga, ogg, ogm, ogv
	"jp", // Covers jpg, jpeg, jp2

	/* video */
	"avi",
	"m4v",
	"m4p",
	"mkv",
	"mov",
	"webm",

	/* audio */
	"wav",
	"m4a",
	"3gp",
	"opus",
	"flac",

	/* image */
	"gif",
	"png",
	"svg",
	"webp",

	/* archives */
	"jar",
	"deb",
	"iso",
	"gz",
	"xz",
	"zst",

	/* others */
	"pdf",
	"pyc", // Python bytecode
	"ttc",
	"ttf",
	"exe",

	/* android */
	"apk",
	"cnt", // Image alias
	"exo", // YouTube
	"odex", // Android RunTime
	"vdex", // Android RunTime
	"so",

	NULL
};

const char *hot_ext_lists[] = {
	"db",

#ifndef WITH_ANDROID
	/* Virtual machines */
	"vmdk", // VMware or VirtualBox
	"vdi", // VirtualBox
	"qcow2", // QEMU
#endif
	NULL
};

const char **default_ext_list[] = {
	media_ext_lists,
	hot_ext_lists
};

static bool is_extension_exist(const char *name)
{
	int i;

	for (i = 0; i < F2FS_MAX_EXTENSION; i++) {
		char *ext = (char *)sb->extension_list[i];
		if (!strcmp(ext, name))
			return 1;
	}

	return 0;
}

static void cure_extension_list(void)
{
	const char **extlist;
	char *ext_str;
	char *ue;
	int name_len;
	int i, pos = 0;

	set_sb(extension_count, 0);
	memset(sb->extension_list, 0, sizeof(sb->extension_list));

	for (i = 0; i < 2; i++) {
		ext_str = c.extension_list[i];
		extlist = default_ext_list[i];

		while (*extlist) {
			name_len = strlen(*extlist);
			memcpy(sb->extension_list[pos++], *extlist, name_len);
			extlist++;
		}
		if (i == 0)
			set_sb(extension_count, pos);
		else
			sb->hot_ext_count = pos - get_sb(extension_count);;

		if (!ext_str)
			continue;

		/* add user ext list */
		ue = strtok(ext_str, ", ");
		while (ue != NULL) {
			name_len = strlen(ue);
			if (name_len >= F2FS_EXTENSION_LEN) {
				MSG(0, "\tWarn: Extension name (%s) is too long\n", ue);
				goto next;
			}
			if (!is_extension_exist(ue))
				memcpy(sb->extension_list[pos++], ue, name_len);
next:
			ue = strtok(NULL, ", ");
			if (pos >= F2FS_MAX_EXTENSION)
				break;
		}

		if (i == 0)
			set_sb(extension_count, pos);
		else
			sb->hot_ext_count = pos - get_sb(extension_count);

		free(c.extension_list[i]);
	}
}

static void verify_cur_segs(void)
{
	int i, j;
	int reorder = 0;

	for (i = 0; i < NR_CURSEG_TYPE; i++) {
		for (j = i + 1; j < NR_CURSEG_TYPE; j++) {
			if (c.cur_seg[i] == c.cur_seg[j]) {
				reorder = 1;
				break;
			}
		}
	}

	if (!reorder)
		return;

	c.cur_seg[0] = 0;
	for (i = 1; i < NR_CURSEG_TYPE; i++)
		c.cur_seg[i] = next_zone(i - 1);
}

static int f2fs_prepare_super_block(void)
{
	uint32_t blk_size_bytes;
	uint32_t log_sectorsize, log_sectors_per_block;
	uint32_t log_blocksize, log_blks_per_seg;
	uint32_t segment_size_bytes, zone_size_bytes;
	uint32_t alignment_bytes;
	uint32_t sit_segments, nat_segments;
	uint32_t blocks_for_sit, blocks_for_nat, blocks_for_ssa;
	uint32_t total_valid_blks_available;
	uint64_t zone_align_start_offset, diff;
	uint64_t total_meta_zones, total_meta_segments;
	uint32_t sit_bitmap_size, max_sit_bitmap_size;
	uint32_t max_nat_bitmap_size, max_nat_segments;
	uint32_t total_zones, avail_zones = 0;
	enum quota_type qtype;
	int i;

	set_sb(magic, F2FS_SUPER_MAGIC);
	set_sb(major_ver, F2FS_MAJOR_VERSION);
	set_sb(minor_ver, F2FS_MINOR_VERSION);

	log_sectorsize = log_base_2(c.sector_size);
	log_sectors_per_block = log_base_2(c.sectors_per_blk);
	log_blocksize = log_sectorsize + log_sectors_per_block;
	log_blks_per_seg = log_base_2(c.blks_per_seg);

	set_sb(log_sectorsize, log_sectorsize);
	set_sb(log_sectors_per_block, log_sectors_per_block);

	set_sb(log_blocksize, log_blocksize);
	set_sb(log_blocks_per_seg, log_blks_per_seg);

	set_sb(segs_per_sec, c.segs_per_sec);
	set_sb(secs_per_zone, c.secs_per_zone);

	blk_size_bytes = 1 << log_blocksize;
	segment_size_bytes = blk_size_bytes * c.blks_per_seg;
	zone_size_bytes =
		blk_size_bytes * c.secs_per_zone *
		c.segs_per_sec * c.blks_per_seg;

	set_sb(checksum_offset, 0);

	set_sb(block_count, c.total_sectors >> log_sectors_per_block);

	alignment_bytes = c.zoned_mode && c.ndevs > 1 ? segment_size_bytes : zone_size_bytes;

	zone_align_start_offset =
		((uint64_t) c.start_sector * DEFAULT_SECTOR_SIZE +
		2 * F2FS_BLKSIZE + alignment_bytes  - 1) /
		alignment_bytes  * alignment_bytes  -
		(uint64_t) c.start_sector * DEFAULT_SECTOR_SIZE;

	if (c.feature & F2FS_FEATURE_RO)
		zone_align_start_offset = 8192;

	if (c.start_sector % DEFAULT_SECTORS_PER_BLOCK) {
		MSG(1, "\t%s: Align start sector number to the page unit\n",
				c.zoned_mode ? "FAIL" : "WARN");
		MSG(1, "\ti.e., start sector: %d, ofs:%d (sects/page: %d)\n",
				c.start_sector,
				c.start_sector % DEFAULT_SECTORS_PER_BLOCK,
				DEFAULT_SECTORS_PER_BLOCK);
		if (c.zoned_mode)
			return -1;
	}

	if (c.zoned_mode && c.ndevs > 1)
		zone_align_start_offset +=
			(c.devices[0].total_sectors * c.sector_size -
			 zone_align_start_offset) % zone_size_bytes;

	set_sb(segment0_blkaddr, zone_align_start_offset / blk_size_bytes);
	sb->cp_blkaddr = sb->segment0_blkaddr;

	MSG(0, "Info: zone aligned segment0 blkaddr: %u\n",
					get_sb(segment0_blkaddr));

	if (c.zoned_mode &&
		((c.ndevs == 1 &&
			(get_sb(segment0_blkaddr) + c.start_sector /
			DEFAULT_SECTORS_PER_BLOCK) % c.zone_blocks) ||
		(c.ndevs > 1 &&
			c.devices[1].start_blkaddr % c.zone_blocks))) {
		MSG(1, "\tError: Unaligned segment0 block address %u\n",
				get_sb(segment0_blkaddr));
		return -1;
	}

	for (i = 0; i < c.ndevs; i++) {
		if (i == 0) {
			c.devices[i].total_segments =
				((c.devices[i].total_sectors *
				c.sector_size - zone_align_start_offset) /
				segment_size_bytes) / c.segs_per_zone *
				c.segs_per_zone;
			c.devices[i].start_blkaddr = 0;
			c.devices[i].end_blkaddr = c.devices[i].total_segments *
						c.blks_per_seg - 1 +
						sb->segment0_blkaddr;
		} else {
			c.devices[i].total_segments =
				(c.devices[i].total_sectors /
				(c.sectors_per_blk * c.blks_per_seg)) /
				c.segs_per_zone * c.segs_per_zone;
			c.devices[i].start_blkaddr =
					c.devices[i - 1].end_blkaddr + 1;
			c.devices[i].end_blkaddr = c.devices[i].start_blkaddr +
					c.devices[i].total_segments *
					c.blks_per_seg - 1;
			if (device_is_aliased(i)) {
				if (c.devices[i].zoned_model ==
						F2FS_ZONED_HM) {
					MSG(1, "\tError: do not support "
					"device aliasing for device[%d]\n", i);
					return -1;
				}
				c.aliased_segments +=
					c.devices[i].total_segments;
			}
		}
		if (c.ndevs > 1) {
			strncpy((char *)sb->devs[i].path, c.devices[i].path, MAX_PATH_LEN);
			sb->devs[i].total_segments =
					cpu_to_le32(c.devices[i].total_segments);
		}

		c.total_segments += c.devices[i].total_segments;
	}
	set_sb(segment_count, c.total_segments);
	set_sb(segment_count_ckpt, F2FS_NUMBER_OF_CHECKPOINT_PACK);

	set_sb(sit_blkaddr, get_sb(segment0_blkaddr) +
			get_sb(segment_count_ckpt) * c.blks_per_seg);

	blocks_for_sit = SIZE_ALIGN(get_sb(segment_count), SIT_ENTRY_PER_BLOCK);

	sit_segments = SEG_ALIGN(blocks_for_sit);

	set_sb(segment_count_sit, sit_segments * 2);

	set_sb(nat_blkaddr, get_sb(sit_blkaddr) + get_sb(segment_count_sit) *
			c.blks_per_seg);

	total_valid_blks_available = (get_sb(segment_count) -
			(get_sb(segment_count_ckpt) +
			get_sb(segment_count_sit))) * c.blks_per_seg;

	blocks_for_nat = SIZE_ALIGN(total_valid_blks_available,
			NAT_ENTRY_PER_BLOCK);

	if (c.large_nat_bitmap) {
		nat_segments = SEG_ALIGN(blocks_for_nat) *
						DEFAULT_NAT_ENTRY_RATIO / 100;
		set_sb(segment_count_nat, nat_segments ? nat_segments : 1);
		max_nat_bitmap_size = (get_sb(segment_count_nat) <<
						log_blks_per_seg) / 8;
		set_sb(segment_count_nat, get_sb(segment_count_nat) * 2);
	} else {
		set_sb(segment_count_nat, SEG_ALIGN(blocks_for_nat));
		max_nat_bitmap_size = 0;
	}

	/*
	 * The number of node segments should not be exceeded a "Threshold".
	 * This number resizes NAT bitmap area in a CP page.
	 * So the threshold is determined not to overflow one CP page
	 */
	sit_bitmap_size = ((get_sb(segment_count_sit) / 2) <<
				log_blks_per_seg) / 8;

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
		max_nat_segments = (max_nat_bitmap_size * 8) >> log_blks_per_seg;

		if (get_sb(segment_count_nat) > max_nat_segments)
			set_sb(segment_count_nat, max_nat_segments);

		set_sb(segment_count_nat, get_sb(segment_count_nat) * 2);
	}

	set_sb(ssa_blkaddr, get_sb(nat_blkaddr) + get_sb(segment_count_nat) *
			c.blks_per_seg);

	total_valid_blks_available = (get_sb(segment_count) -
			(get_sb(segment_count_ckpt) +
			get_sb(segment_count_sit) +
			get_sb(segment_count_nat))) *
			c.blks_per_seg;

	if (c.feature & F2FS_FEATURE_RO)
		blocks_for_ssa = 0;
	else
		blocks_for_ssa = total_valid_blks_available /
				c.blks_per_seg + 1;

	set_sb(segment_count_ssa, SEG_ALIGN(blocks_for_ssa));

	total_meta_segments = get_sb(segment_count_ckpt) +
		get_sb(segment_count_sit) +
		get_sb(segment_count_nat) +
		get_sb(segment_count_ssa);
	diff = total_meta_segments % (c.segs_per_zone);
	if (diff)
		set_sb(segment_count_ssa, get_sb(segment_count_ssa) +
			(c.segs_per_zone - diff));

	total_meta_zones = ZONE_ALIGN(total_meta_segments *
						c.blks_per_seg);

	set_sb(main_blkaddr, get_sb(segment0_blkaddr) + total_meta_zones *
				c.segs_per_zone * c.blks_per_seg);

	if (c.zoned_mode) {
		/*
		 * Make sure there is enough randomly writeable
		 * space at the beginning of the disk.
		 */
		unsigned long main_blkzone = get_sb(main_blkaddr) / c.zone_blocks;

		if (c.devices[0].zoned_model == F2FS_ZONED_HM &&
				c.devices[0].nr_rnd_zones < main_blkzone) {
			MSG(0, "\tError: Device does not have enough random "
					"write zones for F2FS volume (%lu needed)\n",
					main_blkzone);
			return -1;
		}
		/*
		 * Check if conventional device has enough space
		 * to accommodate all metadata, zoned device should
		 * not overlap to metadata area.
		 */
		for (i = 1; i < c.ndevs; i++) {
			if (c.devices[i].zoned_model != F2FS_ZONED_NONE &&
				c.devices[i].start_blkaddr < get_sb(main_blkaddr)) {
				MSG(0, "\tError: Conventional device %s is too small,"
					" (%"PRIu64" MiB needed).\n", c.devices[0].path,
					(get_sb(main_blkaddr) -
					c.devices[i].start_blkaddr) >> 8);
				return -1;
			}
		}
	}

	total_zones = get_sb(segment_count) / (c.segs_per_zone) -
							total_meta_zones;
	if (total_zones == 0)
		goto too_small;
	set_sb(section_count, total_zones * c.secs_per_zone);

	set_sb(segment_count_main, get_sb(section_count) * c.segs_per_sec);

	/*
	 * Let's determine the best reserved and overprovisioned space.
	 * For Zoned device, if zone capacity less than zone size, the segments
	 * starting after the zone capacity are unusable in each zone. So get
	 * overprovision ratio and reserved seg count based on avg usable
	 * segs_per_sec.
	 */
	if (c.overprovision == 0)
		c.overprovision = get_best_overprovision(sb);

	c.reserved_segments = get_reserved(sb, c.overprovision);

	if (c.feature & F2FS_FEATURE_RO) {
		c.overprovision = 0;
		c.reserved_segments = 0;
	}
	if ((!(c.feature & F2FS_FEATURE_RO) &&
		c.overprovision == 0) ||
		c.total_segments < F2FS_MIN_SEGMENTS ||
		(c.devices[0].total_sectors *
			c.sector_size < zone_align_start_offset) ||
		(get_sb(segment_count_main) - NR_CURSEG_TYPE) <
						c.reserved_segments) {
		goto too_small;
	}

	if (c.vol_uuid) {
		if (uuid_parse(c.vol_uuid, sb->uuid)) {
			MSG(0, "\tError: supplied string is not a valid UUID\n");
			return -1;
		}
	} else {
		uuid_generate(sb->uuid);
	}

	/* precompute checksum seed for metadata */
	if (c.feature & F2FS_FEATURE_INODE_CHKSUM)
		c.chksum_seed = f2fs_cal_crc32(~0, sb->uuid, sizeof(sb->uuid));

	utf8_to_utf16((char *)sb->volume_name, (const char *)c.vol_label,
				MAX_VOLUME_NAME, strlen(c.vol_label));
	set_sb(node_ino, 1);
	set_sb(meta_ino, 2);
	set_sb(root_ino, 3);
	c.next_free_nid = 4;

	for (qtype = 0; qtype < F2FS_MAX_QUOTAS; qtype++) {
		if (!((1 << qtype) & c.quota_bits))
			continue;
		sb->qf_ino[qtype] = cpu_to_le32(c.next_free_nid++);
		MSG(0, "Info: add quota type = %u => %u\n",
					qtype, c.next_free_nid - 1);
	}

	if (c.feature & F2FS_FEATURE_LOST_FOUND)
		c.lpf_ino = c.next_free_nid++;

	if (c.aliased_devices) {
		c.first_alias_ino = c.next_free_nid;
		c.next_free_nid += c.aliased_devices;
		avail_zones += c.aliased_segments / c.segs_per_zone;
	}

	if (c.feature & F2FS_FEATURE_RO)
		avail_zones += 2;
	else
		avail_zones += 6;

	if (total_zones <= avail_zones) {
		MSG(1, "\tError: %d zones: Need more zones "
			"by shrinking zone size\n", total_zones);
		return -1;
	}

	if (c.feature & F2FS_FEATURE_RO) {
		c.cur_seg[CURSEG_HOT_NODE] = last_section(last_zone(total_zones));
		c.cur_seg[CURSEG_WARM_NODE] = 0;
		c.cur_seg[CURSEG_COLD_NODE] = 0;
		c.cur_seg[CURSEG_HOT_DATA] = 0;
		c.cur_seg[CURSEG_COLD_DATA] = 0;
		c.cur_seg[CURSEG_WARM_DATA] = 0;
	} else if (c.zoned_mode) {
		c.cur_seg[CURSEG_HOT_NODE] = 0;
		if (c.zoned_model == F2FS_ZONED_HM) {
			uint32_t conv_zones =
				c.devices[0].total_segments / c.segs_per_zone
				- total_meta_zones;

			if (total_zones - conv_zones >= avail_zones)
				c.cur_seg[CURSEG_HOT_NODE] =
					(c.devices[1].start_blkaddr -
					 get_sb(main_blkaddr)) / c.blks_per_seg;
		}
		c.cur_seg[CURSEG_WARM_NODE] = next_zone(CURSEG_HOT_NODE);
		c.cur_seg[CURSEG_COLD_NODE] = next_zone(CURSEG_WARM_NODE);
		c.cur_seg[CURSEG_HOT_DATA] = next_zone(CURSEG_COLD_NODE);
		c.cur_seg[CURSEG_WARM_DATA] = next_zone(CURSEG_HOT_DATA);
		c.cur_seg[CURSEG_COLD_DATA] = next_zone(CURSEG_WARM_DATA);
	} else {
		c.cur_seg[CURSEG_HOT_NODE] = 0;
		c.cur_seg[CURSEG_WARM_NODE] = next_zone(CURSEG_HOT_NODE);
		c.cur_seg[CURSEG_COLD_NODE] = next_zone(CURSEG_WARM_NODE);
		c.cur_seg[CURSEG_HOT_DATA] = next_zone(CURSEG_COLD_NODE);
		c.cur_seg[CURSEG_COLD_DATA] =
				max(last_zone((total_zones >> 2)),
					next_zone(CURSEG_HOT_DATA));
		c.cur_seg[CURSEG_WARM_DATA] =
				max(last_zone((total_zones >> 1)),
					next_zone(CURSEG_COLD_DATA));
	}

	/* if there is redundancy, reassign it */
	if (!(c.feature & F2FS_FEATURE_RO))
		verify_cur_segs();

	cure_extension_list();

	/* get kernel version */
	if (c.kd >= 0) {
		dev_read_version(c.version, 0, VERSION_LEN);
		get_kernel_version(c.version);
	} else {
		get_kernel_uname_version(c.version);
	}
	MSG(0, "Info: format version with\n  \"%s\"\n", c.version);

	memcpy(sb->version, c.version, VERSION_LEN);
	memcpy(sb->init_version, c.version, VERSION_LEN);

	if (c.feature & F2FS_FEATURE_CASEFOLD) {
		set_sb(s_encoding, c.s_encoding);
		set_sb(s_encoding_flags, c.s_encoding_flags);
	}

	sb->feature = cpu_to_le32(c.feature);

	if (c.feature & F2FS_FEATURE_SB_CHKSUM) {
		set_sb(checksum_offset, SB_CHKSUM_OFFSET);
		set_sb(crc, f2fs_cal_crc32(F2FS_SUPER_MAGIC, sb,
						SB_CHKSUM_OFFSET));
		MSG(1, "Info: SB CRC is set: offset (%d), crc (0x%x)\n",
					get_sb(checksum_offset), get_sb(crc));
	}

	return 0;

too_small:
	MSG(0, "\tError: Device size is not sufficient for F2FS volume\n");
	return -1;
}

static int f2fs_init_sit_area(void)
{
	uint32_t blk_size, seg_size;
	uint32_t index = 0;
	uint64_t sit_seg_addr = 0;
	uint8_t *zero_buf = NULL;

	blk_size = 1 << get_sb(log_blocksize);
	seg_size = (1 << get_sb(log_blocks_per_seg)) * blk_size;

	zero_buf = calloc(sizeof(uint8_t), seg_size);
	if(zero_buf == NULL) {
		MSG(1, "\tError: Calloc Failed for sit_zero_buf!!!\n");
		return -1;
	}

	sit_seg_addr = get_sb(sit_blkaddr);
	sit_seg_addr *= blk_size;

	DBG(1, "\tFilling sit area at offset 0x%08"PRIx64"\n", sit_seg_addr);
	for (index = 0; index < (get_sb(segment_count_sit) / 2); index++) {
		if (dev_fill(zero_buf, sit_seg_addr, seg_size, WRITE_LIFE_NONE)) {
			MSG(1, "\tError: While zeroing out the sit area "
					"on disk!!!\n");
			free(zero_buf);
			return -1;
		}
		sit_seg_addr += seg_size;
	}

	free(zero_buf);
	return 0 ;
}

static int f2fs_init_nat_area(void)
{
	uint32_t blk_size, seg_size;
	uint32_t index = 0;
	uint64_t nat_seg_addr = 0;
	uint8_t *nat_buf = NULL;

	blk_size = 1 << get_sb(log_blocksize);
	seg_size = (1 << get_sb(log_blocks_per_seg)) * blk_size;

	nat_buf = calloc(sizeof(uint8_t), seg_size);
	if (nat_buf == NULL) {
		MSG(1, "\tError: Calloc Failed for nat_zero_blk!!!\n");
		return -1;
	}

	nat_seg_addr = get_sb(nat_blkaddr);
	nat_seg_addr *= blk_size;

	DBG(1, "\tFilling nat area at offset 0x%08"PRIx64"\n", nat_seg_addr);
	for (index = 0; index < get_sb(segment_count_nat) / 2; index++) {
		if (dev_fill(nat_buf, nat_seg_addr, seg_size, WRITE_LIFE_NONE)) {
			MSG(1, "\tError: While zeroing out the nat area "
					"on disk!!!\n");
			free(nat_buf);
			return -1;
		}
		nat_seg_addr = nat_seg_addr + (2 * seg_size);
	}

	free(nat_buf);
	return 0 ;
}

static int f2fs_write_check_point_pack(void)
{
	struct f2fs_summary_block *sum;
	struct f2fs_journal *journal;
	uint32_t blk_size_bytes;
	uint32_t nat_bits_bytes, nat_bits_blocks;
	unsigned char *nat_bits = NULL, *empty_nat_bits;
	uint64_t cp_seg_blk = 0;
	uint32_t crc = 0, flags;
	unsigned int i;
	char *cp_payload = NULL;
	char *sum_compact, *sum_compact_p;
	struct f2fs_summary *sum_entry;
	unsigned short vblocks;
	uint32_t used_segments = c.aliased_segments;
	int ret = -1;

	cp = calloc(F2FS_BLKSIZE, 1);
	if (cp == NULL) {
		MSG(1, "\tError: Calloc failed for f2fs_checkpoint!!!\n");
		return ret;
	}

	sum = calloc(F2FS_BLKSIZE, 1);
	if (sum == NULL) {
		MSG(1, "\tError: Calloc failed for summary_node!!!\n");
		goto free_cp;
	}

	sum_compact = calloc(F2FS_BLKSIZE, 1);
	if (sum_compact == NULL) {
		MSG(1, "\tError: Calloc failed for summary buffer!!!\n");
		goto free_sum;
	}
	sum_compact_p = sum_compact;

	nat_bits_bytes = get_sb(segment_count_nat) << 5;
	nat_bits_blocks = F2FS_BYTES_TO_BLK((nat_bits_bytes << 1) + 8 +
						F2FS_BLKSIZE - 1);
	nat_bits = calloc(F2FS_BLKSIZE, nat_bits_blocks);
	if (nat_bits == NULL) {
		MSG(1, "\tError: Calloc failed for nat bits buffer!!!\n");
		goto free_sum_compact;
	}

	cp_payload = calloc(F2FS_BLKSIZE, 1);
	if (cp_payload == NULL) {
		MSG(1, "\tError: Calloc failed for cp_payload!!!\n");
		goto free_nat_bits;
	}

	/* 1. cp page 1 of checkpoint pack 1 */
	srand((c.fake_seed) ? 0 : time(NULL));
	cp->checkpoint_ver = cpu_to_le64(rand() | 0x1);
	set_cp(cur_node_segno[0], c.cur_seg[CURSEG_HOT_NODE]);
	set_cp(cur_node_segno[1], c.cur_seg[CURSEG_WARM_NODE]);
	set_cp(cur_node_segno[2], c.cur_seg[CURSEG_COLD_NODE]);
	set_cp(cur_data_segno[0], c.cur_seg[CURSEG_HOT_DATA]);
	set_cp(cur_data_segno[1], c.cur_seg[CURSEG_WARM_DATA]);
	set_cp(cur_data_segno[2], c.cur_seg[CURSEG_COLD_DATA]);
	for (i = 3; i < MAX_ACTIVE_NODE_LOGS; i++) {
		set_cp(cur_node_segno[i], 0xffffffff);
		set_cp(cur_data_segno[i], 0xffffffff);
	}

	set_cp(cur_node_blkoff[0], c.curseg_offset[CURSEG_HOT_NODE]);
	set_cp(cur_node_blkoff[2], c.curseg_offset[CURSEG_COLD_NODE]);
	set_cp(cur_data_blkoff[0], c.curseg_offset[CURSEG_HOT_DATA]);
	set_cp(cur_data_blkoff[2], c.curseg_offset[CURSEG_COLD_DATA]);
	set_cp(valid_block_count, c.curseg_offset[CURSEG_HOT_NODE] +
			c.curseg_offset[CURSEG_HOT_DATA] +
			c.curseg_offset[CURSEG_COLD_NODE] +
			c.curseg_offset[CURSEG_COLD_DATA] +
			c.aliased_segments * c.blks_per_seg);
	set_cp(rsvd_segment_count, c.reserved_segments);

	/*
	 * For zoned devices, if zone capacity less than zone size, get
	 * overprovision segment count based on usable segments in the device.
	 */
	set_cp(overprov_segment_count, (f2fs_get_usable_segments(sb) -
			get_cp(rsvd_segment_count)) *
			c.overprovision / 100);

	/*
	 * If conf_reserved_sections has a non zero value, overprov_segment_count
	 * is set to overprov_segment_count + rsvd_segment_count.
	 */
	if (c.conf_reserved_sections) {
		/*
		 * Overprovision segments must be bigger than two sections.
		 * In non configurable reserved section case, overprovision
		 * segments are always bigger than two sections.
		 */
		if (get_cp(overprov_segment_count) <
					overprovision_segment_buffer(sb)) {
			MSG(0, "\tError: Not enough overprovision segments (%u)\n",
			    get_cp(overprov_segment_count));
			goto free_cp_payload;
		}
		set_cp(overprov_segment_count, get_cp(overprov_segment_count) +
				get_cp(rsvd_segment_count));
	 } else {
		/*
		 * overprov_segment_count must bigger than rsvd_segment_count.
		 */
		set_cp(overprov_segment_count, max(get_cp(rsvd_segment_count),
			get_cp(overprov_segment_count)) + overprovision_segment_buffer(sb));
	 }

	if (f2fs_get_usable_segments(sb) <= get_cp(overprov_segment_count)) {
		MSG(0, "\tError: Not enough segments to create F2FS Volume\n");
		goto free_cp_payload;
	}
	MSG(0, "Info: Overprovision ratio = %.3lf%%\n", c.overprovision);
	MSG(0, "Info: Overprovision segments = %u (GC reserved = %u)\n",
					get_cp(overprov_segment_count),
					c.reserved_segments);

	/* main segments - reserved segments - (node + data segments) */
	if (c.feature & F2FS_FEATURE_RO)
		used_segments += 2;
	else
		used_segments += 6;

	set_cp(user_block_count, (f2fs_get_usable_segments(sb) -
			get_cp(overprov_segment_count)) * c.blks_per_seg);
	set_cp(free_segment_count, f2fs_get_usable_segments(sb) -
			used_segments);

	/* cp page (2), data summaries (1), node summaries (3) */
	set_cp(cp_pack_total_block_count, 6 + get_sb(cp_payload));
	flags = CP_UMOUNT_FLAG | CP_COMPACT_SUM_FLAG;
	if (!(c.disabled_feature & F2FS_FEATURE_NAT_BITS) &&
			get_cp(cp_pack_total_block_count) <=
			(1 << get_sb(log_blocks_per_seg)) - nat_bits_blocks)
		flags |= CP_NAT_BITS_FLAG;

	if (c.trimmed)
		flags |= CP_TRIMMED_FLAG;

	if (c.large_nat_bitmap)
		flags |= CP_LARGE_NAT_BITMAP_FLAG;

	set_cp(ckpt_flags, flags);
	set_cp(cp_pack_start_sum, 1 + get_sb(cp_payload));
	set_cp(valid_node_count, c.curseg_offset[CURSEG_HOT_NODE] +
			c.curseg_offset[CURSEG_COLD_NODE]);
	set_cp(valid_inode_count, c.curseg_offset[CURSEG_HOT_NODE] +
			c.curseg_offset[CURSEG_COLD_NODE]);
	set_cp(next_free_nid, c.next_free_nid);
	set_cp(sit_ver_bitmap_bytesize, ((get_sb(segment_count_sit) / 2) <<
			get_sb(log_blocks_per_seg)) / 8);

	set_cp(nat_ver_bitmap_bytesize, ((get_sb(segment_count_nat) / 2) <<
			 get_sb(log_blocks_per_seg)) / 8);

	if (c.large_nat_bitmap)
		set_cp(checksum_offset, CP_MIN_CHKSUM_OFFSET);
	else
		set_cp(checksum_offset, CP_CHKSUM_OFFSET);

	crc = f2fs_checkpoint_chksum(cp);
	*((__le32 *)((unsigned char *)cp + get_cp(checksum_offset))) =
							cpu_to_le32(crc);

	blk_size_bytes = 1 << get_sb(log_blocksize);

	if (blk_size_bytes != F2FS_BLKSIZE) {
		MSG(1, "\tError: Wrong block size %d / %d!!!\n",
					blk_size_bytes, F2FS_BLKSIZE);
		goto free_cp_payload;
	}

	cp_seg_blk = get_sb(segment0_blkaddr);

	DBG(1, "\tWriting main segments, cp at offset 0x%08"PRIx64"\n",
						cp_seg_blk);
	if (dev_write_block(cp, cp_seg_blk, WRITE_LIFE_NONE)) {
		MSG(1, "\tError: While writing the cp to disk!!!\n");
		goto free_cp_payload;
	}

	for (i = 0; i < get_sb(cp_payload); i++) {
		cp_seg_blk++;
		if (dev_fill_block(cp_payload, cp_seg_blk, WRITE_LIFE_NONE)) {
			MSG(1, "\tError: While zeroing out the sit bitmap area "
					"on disk!!!\n");
			goto free_cp_payload;
		}
	}

	/* Prepare and write Segment summary for HOT/WARM/COLD DATA
	 *
	 * The structure of compact summary
	 * +-------------------+
	 * | nat_journal       |
	 * +-------------------+
	 * | sit_journal       |
	 * +-------------------+
	 * | hot data summary  |
	 * +-------------------+
	 * | warm data summary |
	 * +-------------------+
	 * | cold data summary |
	 * +-------------------+
	*/

	/* nat_sjournal */
	journal = &c.nat_jnl;
	memcpy(sum_compact_p, &journal->n_nats, SUM_JOURNAL_SIZE);
	sum_compact_p += SUM_JOURNAL_SIZE;

	/* sit_journal */
	journal = &c.sit_jnl;

	if (c.feature & F2FS_FEATURE_RO) {
		i = CURSEG_RO_HOT_DATA;
		vblocks = le16_to_cpu(journal->sit_j.entries[i].se.vblocks);
		journal->sit_j.entries[i].segno = cp->cur_data_segno[0];
		journal->sit_j.entries[i].se.vblocks =
				cpu_to_le16(vblocks | (CURSEG_HOT_DATA << 10));

		i = CURSEG_RO_HOT_NODE;
		vblocks = le16_to_cpu(journal->sit_j.entries[i].se.vblocks);
		journal->sit_j.entries[i].segno = cp->cur_node_segno[0];
		journal->sit_j.entries[i].se.vblocks |=
				cpu_to_le16(vblocks | (CURSEG_HOT_NODE << 10));

		journal->n_sits = cpu_to_le16(2);
	} else {
		for (i = CURSEG_HOT_DATA; i < NR_CURSEG_TYPE; i++) {
			if (i < NR_CURSEG_DATA_TYPE)
				journal->sit_j.entries[i].segno =
					cp->cur_data_segno[i];

			else
				journal->sit_j.entries[i].segno =
					cp->cur_node_segno[i - NR_CURSEG_DATA_TYPE];

			vblocks =
				le16_to_cpu(journal->sit_j.entries[i].se.vblocks);
			journal->sit_j.entries[i].se.vblocks =
						cpu_to_le16(vblocks | (i << 10));
		}

		journal->n_sits = cpu_to_le16(6);
	}

	memcpy(sum_compact_p, &journal->n_sits, SUM_JOURNAL_SIZE);
	sum_compact_p += SUM_JOURNAL_SIZE;

	SET_SUM_TYPE((struct f2fs_summary_block *)sum_compact, SUM_TYPE_DATA);

	/* hot data summary */
	sum_entry = (struct f2fs_summary *)sum_compact_p;
	memcpy(sum_entry, c.sum[CURSEG_HOT_DATA],
			sizeof(struct f2fs_summary) * MAX_CACHE_SUMS);

	/* warm data summary, nothing to do */
	/* cold data summary, nothing to do */

	cp_seg_blk++;
	DBG(1, "\tWriting Segment summary for HOT/WARM/COLD_DATA, at offset 0x%08"PRIx64"\n",
			cp_seg_blk);
	if (dev_write_block(sum_compact, cp_seg_blk, WRITE_LIFE_NONE)) {
		MSG(1, "\tError: While writing the sum_blk to disk!!!\n");
		goto free_cp_payload;
	}

	/* Prepare and write Segment summary for HOT_NODE */
	memset(sum, 0, F2FS_BLKSIZE);
	SET_SUM_TYPE(sum, SUM_TYPE_NODE);
	memcpy(sum->entries, c.sum[CURSEG_HOT_NODE],
			sizeof(struct f2fs_summary) * MAX_CACHE_SUMS);

	cp_seg_blk++;
	DBG(1, "\tWriting Segment summary for HOT_NODE, at offset 0x%08"PRIx64"\n",
			cp_seg_blk);
	if (dev_write_block(sum, cp_seg_blk, WRITE_LIFE_NONE)) {
		MSG(1, "\tError: While writing the sum_blk to disk!!!\n");
		goto free_cp_payload;
	}

	/* Fill segment summary for WARM_NODE to zero. */
	memset(sum, 0, F2FS_BLKSIZE);
	SET_SUM_TYPE(sum, SUM_TYPE_NODE);

	cp_seg_blk++;
	DBG(1, "\tWriting Segment summary for WARM_NODE, at offset 0x%08"PRIx64"\n",
			cp_seg_blk);
	if (dev_write_block(sum, cp_seg_blk, WRITE_LIFE_NONE)) {
		MSG(1, "\tError: While writing the sum_blk to disk!!!\n");
		goto free_cp_payload;
	}

	/* Prepare and write Segment summary for COLD_NODE */
	memset(sum, 0, F2FS_BLKSIZE);
	SET_SUM_TYPE(sum, SUM_TYPE_NODE);
	memcpy(sum->entries, c.sum[CURSEG_COLD_NODE],
			sizeof(struct f2fs_summary) * MAX_CACHE_SUMS);

	cp_seg_blk++;
	DBG(1, "\tWriting Segment summary for COLD_NODE, at offset 0x%08"PRIx64"\n",
			cp_seg_blk);
	if (dev_write_block(sum, cp_seg_blk, WRITE_LIFE_NONE)) {
		MSG(1, "\tError: While writing the sum_blk to disk!!!\n");
		goto free_cp_payload;
	}

	/* cp page2 */
	cp_seg_blk++;
	DBG(1, "\tWriting cp page2, at offset 0x%08"PRIx64"\n", cp_seg_blk);
	if (dev_write_block(cp, cp_seg_blk, WRITE_LIFE_NONE)) {
		MSG(1, "\tError: While writing the cp to disk!!!\n");
		goto free_cp_payload;
	}

	/* write NAT bits, if possible */
	if (flags & CP_NAT_BITS_FLAG) {
		uint32_t i;

		*(__le64 *)nat_bits = get_cp_crc(cp);
		empty_nat_bits = nat_bits + 8 + nat_bits_bytes;
		memset(empty_nat_bits, 0xff, nat_bits_bytes);
		test_and_clear_bit_le(0, empty_nat_bits);

		/* write the last blocks in cp pack */
		cp_seg_blk = get_sb(segment0_blkaddr) + (1 <<
				get_sb(log_blocks_per_seg)) - nat_bits_blocks;

		DBG(1, "\tWriting NAT bits pages, at offset 0x%08"PRIx64"\n",
					cp_seg_blk);

		for (i = 0; i < nat_bits_blocks; i++) {
			if (dev_write_block(nat_bits + i *
						F2FS_BLKSIZE, cp_seg_blk + i,
						WRITE_LIFE_NONE)) {
				MSG(1, "\tError: write NAT bits to disk!!!\n");
				goto free_cp_payload;
			}
		}
	}

	/* cp page 1 of check point pack 2
	 * Initialize other checkpoint pack with version zero
	 */
	cp->checkpoint_ver = 0;

	crc = f2fs_checkpoint_chksum(cp);
	*((__le32 *)((unsigned char *)cp + get_cp(checksum_offset))) =
							cpu_to_le32(crc);
	cp_seg_blk = get_sb(segment0_blkaddr) + c.blks_per_seg;
	DBG(1, "\tWriting cp page 1 of checkpoint pack 2, at offset 0x%08"PRIx64"\n",
				cp_seg_blk);
	if (dev_write_block(cp, cp_seg_blk, WRITE_LIFE_NONE)) {
		MSG(1, "\tError: While writing the cp to disk!!!\n");
		goto free_cp_payload;
	}

	for (i = 0; i < get_sb(cp_payload); i++) {
		cp_seg_blk++;
		if (dev_fill_block(cp_payload, cp_seg_blk, WRITE_LIFE_NONE)) {
			MSG(1, "\tError: While zeroing out the sit bitmap area "
					"on disk!!!\n");
			goto free_cp_payload;
		}
	}

	/* cp page 2 of check point pack 2 */
	cp_seg_blk += (le32_to_cpu(cp->cp_pack_total_block_count) -
					get_sb(cp_payload) - 1);
	DBG(1, "\tWriting cp page 2 of checkpoint pack 2, at offset 0x%08"PRIx64"\n",
				cp_seg_blk);
	if (dev_write_block(cp, cp_seg_blk, WRITE_LIFE_NONE)) {
		MSG(1, "\tError: While writing the cp to disk!!!\n");
		goto free_cp_payload;
	}

	ret = 0;

free_cp_payload:
	free(cp_payload);
free_nat_bits:
	free(nat_bits);
free_sum_compact:
	free(sum_compact);
free_sum:
	free(sum);
free_cp:
	free(cp);
	return ret;
}

static int f2fs_write_super_block(void)
{
	int index;
	uint8_t *zero_buff;

	zero_buff = calloc(F2FS_BLKSIZE, 1);
	if (zero_buff == NULL) {
		MSG(1, "\tError: Calloc Failed for super_blk_zero_buf!!!\n");
		return -1;
	}

	memcpy(zero_buff + F2FS_SUPER_OFFSET, sb, sizeof(*sb));
	DBG(1, "\tWriting super block, at offset 0x%08x\n", 0);
	for (index = 0; index < 2; index++) {
		if (dev_write_block(zero_buff, index, WRITE_LIFE_NONE)) {
			MSG(1, "\tError: While while writing super_blk "
					"on disk!!! index : %d\n", index);
			free(zero_buff);
			return -1;
		}
	}

	free(zero_buff);
	return 0;
}

#ifndef WITH_ANDROID
static int f2fs_discard_obsolete_dnode(void)
{
	struct f2fs_node *raw_node;
	uint64_t next_blkaddr = 0, offset;
	u64 end_blkaddr = (get_sb(segment_count_main) <<
			get_sb(log_blocks_per_seg)) + get_sb(main_blkaddr);
	uint64_t start_inode_pos = get_sb(main_blkaddr);
	uint64_t last_inode_pos;

	if (c.zoned_mode || c.feature & F2FS_FEATURE_RO)
		return 0;

	raw_node = calloc(F2FS_BLKSIZE, 1);
	if (raw_node == NULL) {
		MSG(1, "\tError: Calloc Failed for discard_raw_node!!!\n");
		return -1;
	}

	/* avoid power-off-recovery based on roll-forward policy */
	offset = get_sb(main_blkaddr);
	offset += c.cur_seg[CURSEG_WARM_NODE] * c.blks_per_seg;

	last_inode_pos = start_inode_pos +
		c.cur_seg[CURSEG_HOT_NODE] * c.blks_per_seg +
		c.curseg_offset[CURSEG_COLD_NODE] - 1;

	do {
		if (offset < get_sb(main_blkaddr) || offset >= end_blkaddr)
			break;

		if (dev_read_block(raw_node, offset)) {
			MSG(1, "\tError: While traversing direct node!!!\n");
			free(raw_node);
			return -1;
		}

		next_blkaddr = le32_to_cpu(F2FS_NODE_FOOTER(raw_node)->next_blkaddr);
		memset(raw_node, 0, F2FS_BLKSIZE);

		DBG(1, "\tDiscard dnode, at offset 0x%08"PRIx64"\n", offset);
		if (dev_write_block(raw_node, offset,
				    f2fs_io_type_to_rw_hint(CURSEG_WARM_NODE))) {
			MSG(1, "\tError: While discarding direct node!!!\n");
			free(raw_node);
			return -1;
		}
		offset = next_blkaddr;
		/* should avoid recursive chain due to stale data */
		if (offset >= start_inode_pos || offset <= last_inode_pos)
			break;
	} while (1);

	free(raw_node);
	return 0;
}
#endif

static block_t alloc_next_free_block(int curseg_type)
{
	block_t blkaddr;

	blkaddr = get_sb(main_blkaddr) +
			c.cur_seg[curseg_type] * c.blks_per_seg +
			c.curseg_offset[curseg_type];

	c.curseg_offset[curseg_type]++;

	return blkaddr;
}

void update_sit_journal(int curseg_type)
{
	struct f2fs_journal *sit_jnl = &c.sit_jnl;
	unsigned short vblocks;
	int idx = curseg_type;

	if (c.feature & F2FS_FEATURE_RO) {
		if (curseg_type < NR_CURSEG_DATA_TYPE)
			idx = CURSEG_RO_HOT_DATA;
		else
			idx = CURSEG_RO_HOT_NODE;
	}

	f2fs_set_bit(c.curseg_offset[curseg_type] - 1,
		(char *)sit_jnl->sit_j.entries[idx].se.valid_map);

	vblocks = le16_to_cpu(sit_jnl->sit_j.entries[idx].se.vblocks);
	sit_jnl->sit_j.entries[idx].se.vblocks = cpu_to_le16(vblocks + 1);
}

void update_nat_journal(nid_t nid, block_t blkaddr)
{
	struct f2fs_journal *nat_jnl = &c.nat_jnl;
	unsigned short n_nats = le16_to_cpu(nat_jnl->n_nats);

	nat_jnl->nat_j.entries[n_nats].nid = cpu_to_le32(nid);
	nat_jnl->nat_j.entries[n_nats].ne.version = 0;
	nat_jnl->nat_j.entries[n_nats].ne.ino = cpu_to_le32(nid);
	nat_jnl->nat_j.entries[n_nats].ne.block_addr = cpu_to_le32(blkaddr);
	nat_jnl->n_nats = cpu_to_le16(n_nats + 1);
}

void update_summary_entry(int curseg_type, nid_t nid,
					unsigned short ofs_in_node)
{
	struct f2fs_summary *sum;
	unsigned int curofs = c.curseg_offset[curseg_type] - 1;

	assert(curofs < MAX_CACHE_SUMS);

	sum = c.sum[curseg_type] + curofs;
	sum->nid = cpu_to_le32(nid);
	sum->ofs_in_node = cpu_to_le16(ofs_in_node);
}

static void add_dentry(struct f2fs_dentry_block *dent_blk, unsigned int *didx,
		const char *name, uint32_t ino, u8 type)
{
	int len = strlen(name);
	f2fs_hash_t hash;

	if (name[0] == '.' && (len == 1 || (len == 2 && name[1] == '.')))
		hash = 0;
	else
		hash = f2fs_dentry_hash(0, 0, (unsigned char *)name, len);

	F2FS_DENTRY_BLOCK_DENTRY(dent_blk, *didx).hash_code = cpu_to_le32(hash);
	F2FS_DENTRY_BLOCK_DENTRY(dent_blk, *didx).ino = cpu_to_le32(ino);
	F2FS_DENTRY_BLOCK_DENTRY(dent_blk, *didx).name_len = cpu_to_le16(len);
	F2FS_DENTRY_BLOCK_DENTRY(dent_blk, *didx).file_type = type;

	while (len > F2FS_SLOT_LEN) {
		memcpy(F2FS_DENTRY_BLOCK_FILENAME(dent_blk, *didx), name,
				F2FS_SLOT_LEN);
		test_and_set_bit_le(*didx, dent_blk->dentry_bitmap);
		len -= (int)F2FS_SLOT_LEN;
		name += F2FS_SLOT_LEN;
		(*didx)++;
	}
	memcpy(F2FS_DENTRY_BLOCK_FILENAME(dent_blk, *didx), name, len);
	test_and_set_bit_le(*didx, dent_blk->dentry_bitmap);
	(*didx)++;
}

static block_t f2fs_add_default_dentry_root(void)
{
	struct f2fs_dentry_block *dent_blk = NULL;
	block_t data_blkaddr;
	unsigned int didx = 0;

	dent_blk = calloc(F2FS_BLKSIZE, 1);
	if(dent_blk == NULL) {
		MSG(1, "\tError: Calloc Failed for dent_blk!!!\n");
		return 0;
	}

	add_dentry(dent_blk, &didx, ".",
			le32_to_cpu(sb->root_ino), F2FS_FT_DIR);
	add_dentry(dent_blk, &didx, "..",
			le32_to_cpu(sb->root_ino), F2FS_FT_DIR);

	if (c.lpf_ino)
		add_dentry(dent_blk, &didx, LPF, c.lpf_ino, F2FS_FT_DIR);

	if (c.aliased_devices) {
		int i, dev_off = 0;

		for (i = 1; i < c.ndevs; i++) {
			if (!device_is_aliased(i))
				continue;

			add_dentry(dent_blk, &didx, c.devices[i].alias_filename,
					c.first_alias_ino + dev_off,
					F2FS_FT_REG_FILE);
			dev_off++;
		}
	}

	data_blkaddr = alloc_next_free_block(CURSEG_HOT_DATA);

	DBG(1, "\tWriting default dentry root, at offset 0x%x\n", data_blkaddr);
	if (dev_write_block(dent_blk, data_blkaddr,
			    f2fs_io_type_to_rw_hint(CURSEG_HOT_DATA))) {
		MSG(1, "\tError: While writing the dentry_blk to disk!!!\n");
		free(dent_blk);
		return 0;
	}

	update_sit_journal(CURSEG_HOT_DATA);
	update_summary_entry(CURSEG_HOT_DATA, le32_to_cpu(sb->root_ino), 0);

	free(dent_blk);
	return data_blkaddr;
}

static int f2fs_write_root_inode(void)
{
	struct f2fs_node *raw_node = NULL;
	block_t data_blkaddr;
	block_t node_blkaddr;

	raw_node = calloc(F2FS_BLKSIZE, 1);
	if (raw_node == NULL) {
		MSG(1, "\tError: Calloc Failed for raw_node!!!\n");
		return -1;
	}

	f2fs_init_inode(sb, raw_node, le32_to_cpu(sb->root_ino),
						mkfs_time, 0x41ed);

	if (c.lpf_ino)
		raw_node->i.i_links = cpu_to_le32(3);

	data_blkaddr = f2fs_add_default_dentry_root();
	if (data_blkaddr == 0) {
		MSG(1, "\tError: Failed to add default dentries for root!!!\n");
		free(raw_node);
		return -1;
	}

	raw_node->i.i_addr[get_extra_isize(raw_node)] =
				cpu_to_le32(data_blkaddr);

	node_blkaddr = alloc_next_free_block(CURSEG_HOT_NODE);
	F2FS_NODE_FOOTER(raw_node)->next_blkaddr = cpu_to_le32(node_blkaddr + 1);

	DBG(1, "\tWriting root inode (hot node), offset 0x%x\n", node_blkaddr);
	if (write_inode(raw_node, node_blkaddr,
			f2fs_io_type_to_rw_hint(CURSEG_HOT_NODE)) < 0) {
		MSG(1, "\tError: While writing the raw_node to disk!!!\n");
		free(raw_node);
		return -1;
	}

	update_nat_journal(le32_to_cpu(sb->root_ino), node_blkaddr);
	update_sit_journal(CURSEG_HOT_NODE);
	update_summary_entry(CURSEG_HOT_NODE, le32_to_cpu(sb->root_ino), 0);

	free(raw_node);
	return 0;
}

static int f2fs_write_default_quota(int qtype, __le32 raw_id)
{
	char *filebuf = calloc(F2FS_BLKSIZE, 2);
	int file_magics[] = INITQMAGICS;
	struct v2_disk_dqheader ddqheader;
	struct v2_disk_dqinfo ddqinfo;
	struct v2r1_disk_dqblk dqblk;
	block_t blkaddr;
	uint64_t icnt = 1, bcnt = 1;
	int i;

	if (filebuf == NULL) {
		MSG(1, "\tError: Calloc Failed for filebuf!!!\n");
		return 0;
	}

	/* Write basic quota header */
	ddqheader.dqh_magic = cpu_to_le32(file_magics[qtype]);
	/* only support QF_VFSV1 */
	ddqheader.dqh_version = cpu_to_le32(1);

	memcpy(filebuf, &ddqheader, sizeof(ddqheader));

	/* Fill Initial quota file content */
	ddqinfo.dqi_bgrace = cpu_to_le32(MAX_DQ_TIME);
	ddqinfo.dqi_igrace = cpu_to_le32(MAX_IQ_TIME);
	ddqinfo.dqi_flags = cpu_to_le32(0);
	ddqinfo.dqi_blocks = cpu_to_le32(QT_TREEOFF + 5);
	ddqinfo.dqi_free_blk = cpu_to_le32(0);
	ddqinfo.dqi_free_entry = cpu_to_le32(5);

	memcpy(filebuf + V2_DQINFOOFF, &ddqinfo, sizeof(ddqinfo));

	filebuf[1024] = 2;
	filebuf[2048] = 3;
	filebuf[3072] = 4;
	filebuf[4096] = 5;

	filebuf[5120 + 8] = 1;

	dqblk.dqb_id = raw_id;
	dqblk.dqb_pad = cpu_to_le32(0);
	dqblk.dqb_ihardlimit = cpu_to_le64(0);
	dqblk.dqb_isoftlimit = cpu_to_le64(0);
	if (c.lpf_ino) {
		icnt++;
		bcnt++;
	}
	if (c.aliased_devices) {
		icnt += c.aliased_devices;
		bcnt += c.aliased_segments * c.blks_per_seg;
	}
	dqblk.dqb_curinodes = cpu_to_le64(icnt);
	dqblk.dqb_bhardlimit = cpu_to_le64(0);
	dqblk.dqb_bsoftlimit = cpu_to_le64(0);
	dqblk.dqb_curspace = cpu_to_le64(F2FS_BLKSIZE * bcnt);
	dqblk.dqb_btime = cpu_to_le64(0);
	dqblk.dqb_itime = cpu_to_le64(0);

	memcpy(filebuf + 5136, &dqblk, sizeof(struct v2r1_disk_dqblk));

	/* Write quota blocks */
	for (i = 0; i < QUOTA_DATA; i++) {
		blkaddr = alloc_next_free_block(CURSEG_HOT_DATA);

		if (dev_write_block(filebuf + i * F2FS_BLKSIZE, blkaddr,
				    f2fs_io_type_to_rw_hint(CURSEG_HOT_DATA))) {
			MSG(1, "\tError: While writing the quota_blk to disk!!!\n");
			free(filebuf);
			return 0;
		}

		update_sit_journal(CURSEG_HOT_DATA);
		update_summary_entry(CURSEG_HOT_DATA,
					le32_to_cpu(sb->qf_ino[qtype]), i);
		DBG(1, "\tWriting quota data, at offset %08x (%d/%d)\n",
						blkaddr, i + 1, QUOTA_DATA);

	}

	free(filebuf);
	return blkaddr + 1 - QUOTA_DATA;
}

static int f2fs_write_qf_inode(int qtype)
{
	struct f2fs_node *raw_node = NULL;
	block_t data_blkaddr;
	block_t node_blkaddr;
	__le32 raw_id;
	int i;

	raw_node = calloc(F2FS_BLKSIZE, 1);
	if (raw_node == NULL) {
		MSG(1, "\tError: Calloc Failed for raw_node!!!\n");
		return -1;
	}
	f2fs_init_inode(sb, raw_node,
			le32_to_cpu(sb->qf_ino[qtype]), mkfs_time, 0x8180);

	raw_node->i.i_size = cpu_to_le64(1024 * 6);
	raw_node->i.i_blocks = cpu_to_le64(1 + QUOTA_DATA);
	raw_node->i.i_flags = cpu_to_le32(F2FS_NOATIME_FL | F2FS_IMMUTABLE_FL);

	node_blkaddr = alloc_next_free_block(CURSEG_HOT_NODE);
	F2FS_NODE_FOOTER(raw_node)->next_blkaddr = cpu_to_le32(node_blkaddr + 1);

	if (qtype == 0)
		raw_id = raw_node->i.i_uid;
	else if (qtype == 1)
		raw_id = raw_node->i.i_gid;
	else if (qtype == 2)
		raw_id = raw_node->i.i_projid;
	else
		ASSERT(0);

	/* write quota blocks */
	data_blkaddr = f2fs_write_default_quota(qtype, raw_id);
	if (data_blkaddr == 0) {
		free(raw_node);
		return -1;
	}

	for (i = 0; i < QUOTA_DATA; i++)
		raw_node->i.i_addr[get_extra_isize(raw_node) + i] =
					cpu_to_le32(data_blkaddr + i);

	DBG(1, "\tWriting quota inode (hot node), offset 0x%x\n", node_blkaddr);
	if (write_inode(raw_node, node_blkaddr,
			f2fs_io_type_to_rw_hint(CURSEG_HOT_NODE)) < 0) {
		MSG(1, "\tError: While writing the raw_node to disk!!!\n");
		free(raw_node);
		return -1;
	}

	update_nat_journal(le32_to_cpu(sb->qf_ino[qtype]), node_blkaddr);
	update_sit_journal(CURSEG_HOT_NODE);
	update_summary_entry(CURSEG_HOT_NODE, le32_to_cpu(sb->qf_ino[qtype]), 0);

	free(raw_node);
	return 0;
}

static int f2fs_update_nat_default(void)
{
	struct f2fs_nat_block *nat_blk = NULL;
	uint64_t nat_seg_blk_offset = 0;

	nat_blk = calloc(F2FS_BLKSIZE, 1);
	if(nat_blk == NULL) {
		MSG(1, "\tError: Calloc Failed for nat_blk!!!\n");
		return -1;
	}

	/* update node nat */
	nat_blk->entries[get_sb(node_ino)].block_addr = cpu_to_le32(1);
	nat_blk->entries[get_sb(node_ino)].ino = sb->node_ino;

	/* update meta nat */
	nat_blk->entries[get_sb(meta_ino)].block_addr = cpu_to_le32(1);
	nat_blk->entries[get_sb(meta_ino)].ino = sb->meta_ino;

	nat_seg_blk_offset = get_sb(nat_blkaddr);

	DBG(1, "\tWriting nat root, at offset 0x%08"PRIx64"\n",
					nat_seg_blk_offset);
	if (dev_write_block(nat_blk, nat_seg_blk_offset, WRITE_LIFE_NONE)) {
		MSG(1, "\tError: While writing the nat_blk set0 to disk!\n");
		free(nat_blk);
		return -1;
	}

	free(nat_blk);
	return 0;
}

static block_t f2fs_add_default_dentry_lpf(void)
{
	struct f2fs_dentry_block *dent_blk;
	block_t data_blkaddr;
	unsigned int didx = 0;

	dent_blk = calloc(F2FS_BLKSIZE, 1);
	if (dent_blk == NULL) {
		MSG(1, "\tError: Calloc Failed for dent_blk!!!\n");
		return 0;
	}

	add_dentry(dent_blk, &didx, ".", c.lpf_ino, F2FS_FT_DIR);
	add_dentry(dent_blk, &didx, "..", c.lpf_ino, F2FS_FT_DIR);

	data_blkaddr = alloc_next_free_block(CURSEG_HOT_DATA);

	DBG(1, "\tWriting default dentry lost+found, at offset 0x%x\n",
							data_blkaddr);
	if (dev_write_block(dent_blk, data_blkaddr,
			    f2fs_io_type_to_rw_hint(CURSEG_HOT_DATA))) {
		MSG(1, "\tError While writing the dentry_blk to disk!!!\n");
		free(dent_blk);
		return 0;
	}

	update_sit_journal(CURSEG_HOT_DATA);
	update_summary_entry(CURSEG_HOT_DATA, c.lpf_ino, 0);

	free(dent_blk);
	return data_blkaddr;
}

static int f2fs_write_lpf_inode(void)
{
	struct f2fs_node *raw_node;
	block_t data_blkaddr;
	block_t node_blkaddr;
	int err = 0;

	ASSERT(c.lpf_ino);

	raw_node = calloc(F2FS_BLKSIZE, 1);
	if (raw_node == NULL) {
		MSG(1, "\tError: Calloc Failed for raw_node!!!\n");
		return -1;
	}

	f2fs_init_inode(sb, raw_node, c.lpf_ino, mkfs_time, 0x41c0);

	raw_node->i.i_pino = sb->root_ino;
	raw_node->i.i_namelen = cpu_to_le32(strlen(LPF));
	memcpy(raw_node->i.i_name, LPF, strlen(LPF));

	node_blkaddr = alloc_next_free_block(CURSEG_HOT_NODE);
	F2FS_NODE_FOOTER(raw_node)->next_blkaddr = cpu_to_le32(node_blkaddr + 1);

	data_blkaddr = f2fs_add_default_dentry_lpf();
	if (data_blkaddr == 0) {
		MSG(1, "\tError: Failed to add default dentries for lost+found!!!\n");
		err = -1;
		goto exit;
	}
	raw_node->i.i_addr[get_extra_isize(raw_node)] = cpu_to_le32(data_blkaddr);

	DBG(1, "\tWriting lost+found inode (hot node), offset 0x%x\n",
								node_blkaddr);
	if (write_inode(raw_node, node_blkaddr,
			f2fs_io_type_to_rw_hint(CURSEG_HOT_NODE)) < 0) {
		MSG(1, "\tError: While writing the raw_node to disk!!!\n");
		err = -1;
		goto exit;
	}

	update_nat_journal(c.lpf_ino, node_blkaddr);
	update_sit_journal(CURSEG_HOT_NODE);
	update_summary_entry(CURSEG_HOT_NODE, c.lpf_ino, 0);

exit:
	free(raw_node);
	return err;
}

static void allocate_blocks_for_aliased_device(struct f2fs_node *raw_node,
		unsigned int dev_num)
{
	uint32_t start_segno = (c.devices[dev_num].start_blkaddr -
			get_sb(main_blkaddr)) / c.blks_per_seg;
	uint32_t end_segno = (c.devices[dev_num].end_blkaddr -
			get_sb(main_blkaddr) + 1) / c.blks_per_seg;
	uint32_t segno;
	uint64_t blkcnt;
	struct f2fs_sit_block *sit_blk = calloc(F2FS_BLKSIZE, 1);

	ASSERT(sit_blk);

	for (segno = start_segno; segno < end_segno; segno++) {
		struct f2fs_sit_entry *sit;
		uint64_t sit_blk_addr = get_sb(sit_blkaddr) +
			(segno / SIT_ENTRY_PER_BLOCK);

		ASSERT(dev_read_block(sit_blk, sit_blk_addr) >= 0);
		sit = &sit_blk->entries[segno % SIT_ENTRY_PER_BLOCK];
		memset(&sit->valid_map, 0xFF, SIT_VBLOCK_MAP_SIZE);
		sit->vblocks = cpu_to_le16((CURSEG_COLD_DATA <<
					SIT_VBLOCKS_SHIFT) | c.blks_per_seg);
		sit->mtime = cpu_to_le64(mkfs_time);
		ASSERT(dev_write_block(sit_blk, sit_blk_addr,
			f2fs_io_type_to_rw_hint(CURSEG_COLD_DATA)) >= 0);
	}

	blkcnt = (end_segno - start_segno) * c.blks_per_seg;
	raw_node->i.i_size = cpu_to_le64(blkcnt << get_sb(log_blocksize));
	raw_node->i.i_blocks = cpu_to_le64(blkcnt + 1);

	raw_node->i.i_ext.fofs = cpu_to_le32(0);
	raw_node->i.i_ext.blk_addr =
		cpu_to_le32(c.devices[dev_num].start_blkaddr);
	raw_node->i.i_ext.len = cpu_to_le32(blkcnt);

	free(sit_blk);
}

static int f2fs_write_alias_inodes(void)
{
	struct f2fs_node *raw_node;
	block_t node_blkaddr;
	int err = 0;
	unsigned int i, dev_off = 0;

	ASSERT(c.aliased_devices);

	raw_node = calloc(F2FS_BLKSIZE, 1);
	if (raw_node == NULL) {
		MSG(1, "\tError: Calloc Failed for raw_node!!!\n");
		return -1;
	}

	for (i = 1; i < c.ndevs; i++) {
		const char *filename;
		nid_t ino;

		if (!device_is_aliased(i))
			continue;

		ino = c.first_alias_ino + dev_off;
		dev_off++;
		f2fs_init_inode(sb, raw_node, ino, mkfs_time, 0x81c0);

		raw_node->i.i_flags = cpu_to_le32(F2FS_DEVICE_ALIAS_FL);
		raw_node->i.i_inline = F2FS_PIN_FILE;
		raw_node->i.i_pino = sb->root_ino;
		filename = c.devices[i].alias_filename;
		raw_node->i.i_namelen = cpu_to_le32(strlen(filename));
		memcpy(raw_node->i.i_name, filename, strlen(filename));

		node_blkaddr = alloc_next_free_block(CURSEG_COLD_NODE);
		F2FS_NODE_FOOTER(raw_node)->next_blkaddr =
			cpu_to_le32(node_blkaddr + 1);

		allocate_blocks_for_aliased_device(raw_node, i);

		DBG(1, "\tWriting aliased device inode (cold node), "
				"offset 0x%x\n", node_blkaddr);
		if (write_inode(raw_node, node_blkaddr,
			    f2fs_io_type_to_rw_hint(CURSEG_COLD_NODE)) < 0) {
			MSG(1, "\tError: While writing the raw_node to "
					"disk!!!\n");
			err = -1;
			goto exit;
		}

		update_nat_journal(ino, node_blkaddr);
		update_sit_journal(CURSEG_COLD_NODE);
		update_summary_entry(CURSEG_COLD_NODE, ino, 0);
	}

exit:
	free(raw_node);
	return err;
}

static int f2fs_create_root_dir(void)
{
	enum quota_type qtype;
	int err = 0;

	err = f2fs_write_root_inode();
	if (err < 0) {
		MSG(1, "\tError: Failed to write root inode!!!\n");
		goto exit;
	}

	for (qtype = 0; qtype < F2FS_MAX_QUOTAS; qtype++)  {
		if (!((1 << qtype) & c.quota_bits))
			continue;
		err = f2fs_write_qf_inode(qtype);
		if (err < 0) {
			MSG(1, "\tError: Failed to write quota inode!!!\n");
			goto exit;
		}
	}

	if (c.feature & F2FS_FEATURE_LOST_FOUND) {
		err = f2fs_write_lpf_inode();
		if (err < 0) {
			MSG(1, "\tError: Failed to write lost+found inode!!!\n");
			goto exit;
		}
	}

	if (c.aliased_devices) {
		err = f2fs_write_alias_inodes();
		if (err < 0) {
			MSG(1, "\tError: Failed to write aliased device "
				"inodes!!!\n");
			goto exit;
		}
	}

#ifndef WITH_ANDROID
	err = f2fs_discard_obsolete_dnode();
	if (err < 0) {
		MSG(1, "\tError: Failed to discard obsolete dnode!!!\n");
		goto exit;
	}
#endif

	err = f2fs_update_nat_default();
	if (err < 0) {
		MSG(1, "\tError: Failed to update NAT for root!!!\n");
		goto exit;
	}
exit:
	if (err)
		MSG(1, "\tError: Could not create the root directory!!!\n");

	return err;
}

int f2fs_format_device(void)
{
	int err = 0;

	err= f2fs_prepare_super_block();
	if (err < 0) {
		MSG(0, "\tError: Failed to prepare a super block!!!\n");
		goto exit;
	}

	if (c.trim) {
		err = f2fs_trim_devices();
		if (err < 0) {
			MSG(0, "\tError: Failed to trim whole device!!!\n");
			goto exit;
		}
	}

	err = f2fs_init_sit_area();
	if (err < 0) {
		MSG(0, "\tError: Failed to initialise the SIT AREA!!!\n");
		goto exit;
	}

	err = f2fs_init_nat_area();
	if (err < 0) {
		MSG(0, "\tError: Failed to initialise the NAT AREA!!!\n");
		goto exit;
	}

	err = f2fs_create_root_dir();
	if (err < 0) {
		MSG(0, "\tError: Failed to create the root directory!!!\n");
		goto exit;
	}

	err = f2fs_write_check_point_pack();
	if (err < 0) {
		MSG(0, "\tError: Failed to write the check point pack!!!\n");
		goto exit;
	}

	err = f2fs_write_super_block();
	if (err < 0) {
		MSG(0, "\tError: Failed to write the super block!!!\n");
		goto exit;
	}
exit:
	if (err)
		MSG(0, "\tError: Could not format the device!!!\n");

	return err;
}
