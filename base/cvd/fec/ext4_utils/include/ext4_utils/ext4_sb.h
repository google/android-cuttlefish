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

#ifndef _EXT4_UTILS_EXT4_SB_H_
#define _EXT4_UTILS_EXT4_SB_H_

#include "ext4_utils/ext4_kernel_headers.h"

#define EXT4_SUPER_MAGIC 0xEF53

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

struct fs_info {
    int64_t len; /* If set to 0, ask the block device for the size,
                  * if less than 0, reserve that much space at the
                  * end of the partition, else use the size given. */
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t flash_erase_block_size;
    uint32_t flash_logical_block_size;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t inodes;
    uint32_t journal_blocks;
    uint32_t feat_ro_compat;
    uint32_t feat_compat;
    uint32_t feat_incompat;
    uint32_t bg_desc_reserve_blocks;
    uint16_t bg_desc_size;
    const char* label;
    uint8_t no_journal;
    bool block_device; /* target fd is a block device? */
};

int ext4_parse_sb(struct ext4_super_block* sb, struct fs_info* info);

#ifdef __cplusplus
}
#endif

#endif
