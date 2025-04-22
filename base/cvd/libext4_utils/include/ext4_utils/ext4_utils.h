/*
 * Copyright (C) 2010 The Android Open Source Project
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

#ifndef _EXT4_UTILS_H_
#define _EXT4_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <unistd.h>

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "ext4_sb.h"

extern int force;

#define EXT4_JNL_BACKUP_BLOCKS 1

#ifndef __cplusplus
#ifndef min /* already defined by windows.h */
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#endif

#define DIV_ROUND_UP(x, y) (((x) + (y)-1) / (y))
#define EXT4_ALIGN(x, y) ((y)*DIV_ROUND_UP((x), (y)))

/* XXX */
#define cpu_to_le32(x) (x)
#define cpu_to_le16(x) (x)
#define le32_to_cpu(x) (x)
#define le16_to_cpu(x) (x)

#ifdef __LP64__
typedef unsigned long u64;
typedef signed long s64;

#define PRIext4u64 "lu"
#else
typedef unsigned long long u64;
typedef signed long long s64;

#define PRIext4u64 PRIu64
#endif
typedef unsigned int u32;
typedef unsigned short int u16;
typedef unsigned char u8;

struct block_group_info;
struct xattr_list_element;

struct ext2_group_desc {
    u64 bg_block_bitmap;
    u64 bg_inode_bitmap;
    u64 bg_inode_table;
    u32 bg_free_blocks_count;
    u32 bg_free_inodes_count;
    u32 bg_used_dirs_count;
    u16 bg_flags;
};

struct fs_aux_info {
    struct ext4_super_block* sb;
    struct ext4_super_block* sb_block;
    struct ext4_super_block* sb_zero;
    struct ext4_super_block** backup_sb;
    struct ext2_group_desc* bg_desc;
    struct block_group_info* bgs;
    struct xattr_list_element* xattrs;
    u32 first_data_block;
    u64 len_blocks;
    u32 inode_table_blocks;
    u32 groups;
    u32 bg_desc_blocks;
    u32 default_i_flags;
    u64 blocks_per_ind;
    u64 blocks_per_dind;
    u64 blocks_per_tind;
};

extern struct fs_info info;
extern struct fs_aux_info aux_info;

extern jmp_buf setjmp_env;

int bitmap_get_bit(u8* bitmap, u32 bit);  // vold
u64 get_block_device_size(int fd);        // recovery
int is_block_device_fd(int fd);           // wipe.c
u64 get_file_size(int fd);                // fs_mgr
int ext4_bg_has_super_block(int bg);
int read_ext(int fd, int verbose);  // vold

#ifdef __cplusplus
}
#endif

#endif
