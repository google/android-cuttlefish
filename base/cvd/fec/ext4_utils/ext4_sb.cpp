/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>

#include "ext4_utils/ext4_sb.h"

int ext4_parse_sb(struct ext4_super_block* sb, struct fs_info* info) {
    uint64_t len_blocks;

    if (sb->s_magic != EXT4_SUPER_MAGIC) return -EINVAL;

    info->block_size = 1024 << sb->s_log_block_size;
    info->blocks_per_group = sb->s_blocks_per_group;
    info->inodes_per_group = sb->s_inodes_per_group;
    info->inode_size = sb->s_inode_size;
    info->inodes = sb->s_inodes_count;
    info->feat_ro_compat = sb->s_feature_ro_compat;
    info->feat_compat = sb->s_feature_compat;
    info->feat_incompat = sb->s_feature_incompat;
    info->bg_desc_reserve_blocks = sb->s_reserved_gdt_blocks;
    info->bg_desc_size = (sb->s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT)
                                 ? sb->s_desc_size
                                 : EXT4_MIN_DESC_SIZE;
    info->label = sb->s_volume_name;

    len_blocks = ((uint64_t)sb->s_blocks_count_hi << 32) + sb->s_blocks_count_lo;
    info->len = (uint64_t)info->block_size * len_blocks;

    return 0;
}
