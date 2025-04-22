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

#include "ext4_utils/ext4_utils.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/ioctl.h>
#endif

#if defined(__linux__)
#include <linux/fs.h>
#elif defined(__APPLE__) && defined(__MACH__)
#include <sys/disk.h>
#endif

#include "helpers.h"

int force = 0;
struct fs_info info;
struct fs_aux_info aux_info;

jmp_buf setjmp_env;

/* returns 1 if a is a power of b */
static int is_power_of(int a, int b) {
    while (a > b) {
        if (a % b) return 0;
        a /= b;
    }

    return (a == b) ? 1 : 0;
}

int bitmap_get_bit(u8* bitmap, u32 bit) {
    if (bitmap[bit / 8] & (1 << (bit % 8))) return 1;

    return 0;
}

void bitmap_clear_bit(u8* bitmap, u32 bit) {
    bitmap[bit / 8] &= ~(1 << (bit % 8));

    return;
}

/* Returns 1 if the bg contains a backup superblock.  On filesystems with
   the sparse_super feature, only block groups 0, 1, and powers of 3, 5,
   and 7 have backup superblocks.  Otherwise, all block groups have backup
   superblocks */
int ext4_bg_has_super_block(int bg) {
    /* Without sparse_super, every block group has a superblock */
    if (!(info.feat_ro_compat & EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER)) return 1;

    if (bg == 0 || bg == 1) return 1;

    if (is_power_of(bg, 3) || is_power_of(bg, 5) || is_power_of(bg, 7)) return 1;

    return 0;
}

/* Function to read the primary superblock */
void read_sb(int fd, struct ext4_super_block* sb) {
    if (lseek(fd, 1024, SEEK_SET) < 0) critical_error_errno("failed to seek to superblock");

    ssize_t ret = read(fd, sb, sizeof(*sb));
    if (ret < 0) critical_error_errno("failed to read superblock");
    if (ret != sizeof(*sb)) critical_error("failed to read all of superblock");
}

/* Compute the rest of the parameters of the filesystem from the basic info */
void ext4_create_fs_aux_info() {
    aux_info.first_data_block = (info.block_size > 1024) ? 0 : 1;
    aux_info.len_blocks = info.len / info.block_size;
    aux_info.inode_table_blocks =
            DIV_ROUND_UP(info.inodes_per_group * info.inode_size, info.block_size);
    aux_info.groups =
            DIV_ROUND_UP(aux_info.len_blocks - aux_info.first_data_block, info.blocks_per_group);
    aux_info.blocks_per_ind = info.block_size / sizeof(u32);
    aux_info.blocks_per_dind = aux_info.blocks_per_ind * aux_info.blocks_per_ind;
    aux_info.blocks_per_tind = aux_info.blocks_per_dind * aux_info.blocks_per_dind;

    aux_info.bg_desc_blocks =
            DIV_ROUND_UP(aux_info.groups * (size_t)info.bg_desc_size, info.block_size);

    aux_info.default_i_flags = EXT4_NOATIME_FL;

    u32 last_group_size = aux_info.len_blocks == info.blocks_per_group
                                  ? aux_info.len_blocks
                                  : aux_info.len_blocks % info.blocks_per_group;
    u32 last_header_size = 2 + aux_info.inode_table_blocks;
    if (ext4_bg_has_super_block((int)aux_info.groups - 1))
        last_header_size += 1 + aux_info.bg_desc_blocks + info.bg_desc_reserve_blocks;
    if (aux_info.groups <= 1 && last_group_size < last_header_size) {
        critical_error("filesystem size too small");
    }
    if (last_group_size > 0 && last_group_size < last_header_size) {
        aux_info.groups--;
        aux_info.len_blocks -= last_group_size;
    }

    /* A zero-filled superblock to be written firstly to the block
     * device to mark the file-system as invalid
     */
    aux_info.sb_zero = (struct ext4_super_block*)calloc(1, info.block_size);
    if (!aux_info.sb_zero) critical_error_errno("calloc");

    /* The write_data* functions expect only block aligned calls.
     * This is not an issue, except when we write out the super
     * block on a system with a block size > 1K.  So, we need to
     * deal with that here.
     */
    aux_info.sb_block = (struct ext4_super_block*)calloc(1, info.block_size);
    if (!aux_info.sb_block) critical_error_errno("calloc");

    if (info.block_size > 1024)
        aux_info.sb = (struct ext4_super_block*)((char*)aux_info.sb_block + 1024);
    else
        aux_info.sb = aux_info.sb_block;

    /* Alloc an array to hold the pointers to the backup superblocks */
    aux_info.backup_sb =
            (struct ext4_super_block**)calloc(aux_info.groups, sizeof(struct ext4_super_block*));

    if (!aux_info.sb) critical_error_errno("calloc");

    aux_info.bg_desc =
            (struct ext2_group_desc*)calloc(aux_info.groups, sizeof(struct ext2_group_desc));
    if (!aux_info.bg_desc) critical_error_errno("calloc");
    aux_info.xattrs = NULL;
}

void ext4_free_fs_aux_info() {
    unsigned int i;

    for (i = 0; i < aux_info.groups; i++) {
        if (aux_info.backup_sb[i]) free(aux_info.backup_sb[i]);
    }
    free(aux_info.sb_block);
    free(aux_info.sb_zero);
    free(aux_info.bg_desc);
}

void ext4_parse_sb_info(struct ext4_super_block* sb) {
    if (sb->s_magic != EXT4_SUPER_MAGIC) error("superblock magic incorrect");

    if ((sb->s_state & EXT4_VALID_FS) != EXT4_VALID_FS) error("filesystem state not valid");

    ext4_parse_sb(sb, &info);

    ext4_create_fs_aux_info();

    memcpy(aux_info.sb, sb, sizeof(*sb));

    if (aux_info.first_data_block != sb->s_first_data_block)
        critical_error("first data block does not match");
}

u64 get_block_device_size(int fd) {
    u64 size = 0;
    int ret;

#if defined(__linux__)
    ret = ioctl(fd, BLKGETSIZE64, &size);
#elif defined(__APPLE__) && defined(__MACH__)
    ret = ioctl(fd, DKIOCGETBLOCKCOUNT, &size);
#else
    close(fd);
    return 0;
#endif

    if (ret) return 0;

    return size;
}

int is_block_device_fd(int fd __attribute__((unused))) {
#ifdef _WIN32
    return 0;
#else
    struct stat st;
    int ret = fstat(fd, &st);
    if (ret < 0) return 0;

    return S_ISBLK(st.st_mode);
#endif
}

u64 get_file_size(int fd) {
    struct stat buf;
    int ret;
    u64 reserve_len = 0;
    s64 computed_size;

    ret = fstat(fd, &buf);
    if (ret) return 0;

    if (info.len < 0) reserve_len = -info.len;

    if (S_ISREG(buf.st_mode))
        computed_size = buf.st_size - reserve_len;
    else if (S_ISBLK(buf.st_mode))
        computed_size = get_block_device_size(fd) - reserve_len;
    else
        computed_size = 0;

    if (computed_size < 0) {
        warn("Computed filesystem size less than 0");
        computed_size = 0;
    }

    return computed_size;
}

static void read_block_group_descriptors(int fd) {
    size_t size = info.block_size * (size_t)aux_info.bg_desc_blocks;
    void* buf = malloc(size);
    ssize_t ret;

    if (!buf) critical_error("failed to alloc buffer");

    ret = read(fd, buf, size);
    if (ret < 0) {
        free(buf);
        critical_error_errno("failed to read block group descriptors");
    }
    if (ret != size) {
        free(buf);
        critical_error("failed to read all the block group descriptors");
    }
    const struct ext4_group_desc* gdp = (const struct ext4_group_desc*)buf;
    bool extended = (info.bg_desc_size >= EXT4_MIN_DESC_SIZE_64BIT);
    for (size_t i = 0; i < aux_info.groups; i++) {
        aux_info.bg_desc[i].bg_block_bitmap =
                (extended ? (u64)gdp->bg_block_bitmap_hi << 32 : 0) | gdp->bg_block_bitmap_lo;
        aux_info.bg_desc[i].bg_inode_bitmap =
                (extended ? (u64)gdp->bg_inode_bitmap_hi << 32 : 0) | gdp->bg_inode_bitmap_lo;
        aux_info.bg_desc[i].bg_inode_table =
                (extended ? (u64)gdp->bg_inode_table_hi << 32 : 0) | gdp->bg_inode_table_lo;
        aux_info.bg_desc[i].bg_free_blocks_count =
                (extended ? (u32)gdp->bg_free_blocks_count_hi << 16 : 0) |
                gdp->bg_free_blocks_count_lo;
        aux_info.bg_desc[i].bg_free_inodes_count =
                (extended ? (u32)gdp->bg_free_inodes_count_hi << 16 : 0) |
                gdp->bg_free_inodes_count_lo;
        aux_info.bg_desc[i].bg_used_dirs_count =
                (extended ? (u32)gdp->bg_used_dirs_count_hi << 16 : 0) | gdp->bg_used_dirs_count_lo;
        aux_info.bg_desc[i].bg_flags = gdp->bg_flags;
        gdp = (const struct ext4_group_desc*)((u8*)gdp + info.bg_desc_size);
    }
    free(buf);
}

int read_ext(int fd, int verbose) {
    off_t ret;
    struct ext4_super_block sb;

    read_sb(fd, &sb);

    ext4_parse_sb_info(&sb);

    ret = lseek(fd, info.len, SEEK_SET);
    if (ret < 0) critical_error_errno("failed to seek to end of input image");

    ret = lseek(fd, info.block_size * (aux_info.first_data_block + 1), SEEK_SET);
    if (ret < 0) critical_error_errno("failed to seek to block group descriptors");

    read_block_group_descriptors(fd);

    if (verbose) {
        printf("Found filesystem with parameters:\n");
        printf("    Size: %" PRIu64 "\n", info.len);
        printf("    Block size: %d\n", info.block_size);
        printf("    Blocks per group: %d\n", info.blocks_per_group);
        printf("    Inodes per group: %d\n", info.inodes_per_group);
        printf("    Inode size: %d\n", info.inode_size);
        printf("    Label: %s\n", info.label);
        printf("    Blocks: %" PRIext4u64 "\n", aux_info.len_blocks);
        printf("    Block groups: %d\n", aux_info.groups);
        printf("    Reserved block group size: %d\n", info.bg_desc_reserve_blocks);
        printf("    Block group descriptor size: %d\n", info.bg_desc_size);
        printf("    Used %d/%d inodes and %d/%d blocks\n",
               aux_info.sb->s_inodes_count - aux_info.sb->s_free_inodes_count,
               aux_info.sb->s_inodes_count,
               aux_info.sb->s_blocks_count_lo - aux_info.sb->s_free_blocks_count_lo,
               aux_info.sb->s_blocks_count_lo);
    }

    return 0;
}
