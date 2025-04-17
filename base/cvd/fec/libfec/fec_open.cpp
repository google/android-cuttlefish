/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <ext4_utils/ext4_sb.h>
#include <squashfs_utils.h>

#if defined(__linux__)
    #include <linux/fs.h>
#elif defined(__APPLE__)
    #include <sys/disk.h>
    #define BLKGETSIZE64 DKIOCGETBLOCKCOUNT
    #define fdatasync(fd) fcntl((fd), F_FULLFSYNC)
#endif

#include "avb_utils.h"
#include "fec_private.h"

/* used by `find_offset'; returns metadata size for a file size `size' and
   `roots' Reed-Solomon parity bytes */
using size_func = uint64_t (*)(uint64_t size, int roots);

/* performs a binary search to find a metadata offset from a file so that
   the metadata size matches function `get_real_size(size, roots)', using
   the approximate size returned by `get_appr_size' as a starting point */
static int find_offset(uint64_t file_size, int roots, uint64_t *offset,
        size_func get_appr_size, size_func get_real_size)
{
    check(offset);
    check(get_appr_size);
    check(get_real_size);

    if (file_size % FEC_BLOCKSIZE) {
        /* must be a multiple of block size */
        error("file size not multiple of " stringify(FEC_BLOCKSIZE));
        errno = EINVAL;
        return -1;
    }

    uint64_t mi = get_appr_size(file_size, roots);
    uint64_t lo = file_size - mi * 2;
    uint64_t hi = file_size - mi / 2;

    while (lo < hi) {
        mi = ((hi + lo) / (2 * FEC_BLOCKSIZE)) * FEC_BLOCKSIZE;
        uint64_t total = mi + get_real_size(mi, roots);

        if (total < file_size) {
            lo = mi + FEC_BLOCKSIZE;
        } else if (total > file_size) {
            hi = mi;
        } else {
            *offset = mi;
            debug("file_size = %" PRIu64 " -> offset = %" PRIu64, file_size,
                mi);
            return 0;
        }
    }

    warn("could not determine offset");
    errno = ERANGE;
    return -1;
}

/* returns verity metadata size for a `size' byte file */
static uint64_t get_verity_size(uint64_t size, int)
{
    return VERITY_METADATA_SIZE +
           verity_get_size(size, NULL, NULL, SHA256_DIGEST_LENGTH);
}

/* computes the verity metadata offset for a file with size `f->size' */
static int find_verity_offset(fec_handle *f, uint64_t *offset)
{
    check(f);
    check(offset);

    return find_offset(f->data_size, 0, offset, get_verity_size,
                get_verity_size);
}

/* attempts to read and validate an ecc header from file position `offset' */
static int parse_ecc_header(fec_handle *f, uint64_t offset)
{
    check(f);
    check(f->ecc.rsn > 0 && f->ecc.rsn < FEC_RSM);
    check(f->size > sizeof(fec_header));

    debug("offset = %" PRIu64, offset);

    if (offset > f->size - sizeof(fec_header)) {
        return -1;
    }

    fec_header header;

    /* there's obviously no ecc data at this point, so there is no need to
       call fec_pread to access this data */
    if (!raw_pread(f->fd, &header, sizeof(fec_header), offset)) {
        error("failed to read: %s", strerror(errno));
        return -1;
    }

    /* move offset back to the beginning of the block for validating header */
    offset -= offset % FEC_BLOCKSIZE;

    if (header.magic != FEC_MAGIC) {
        return -1;
    }
    if (header.version != FEC_VERSION) {
        error("unsupported ecc version: %u", header.version);
        return -1;
    }
    if (header.size != sizeof(fec_header)) {
        error("unexpected ecc header size: %u", header.size);
        return -1;
    }
    if (header.roots == 0 || header.roots >= FEC_RSM) {
        error("invalid ecc roots: %u", header.roots);
        return -1;
    }
    if (f->ecc.roots != (int)header.roots) {
        error("unexpected number of roots: %d vs %u", f->ecc.roots,
            header.roots);
        return -1;
    }
    if (header.fec_size % header.roots ||
            header.fec_size % FEC_BLOCKSIZE) {
        error("inconsistent ecc size %u", header.fec_size);
        return -1;
    }

    f->data_size = header.inp_size;
    f->ecc.blocks = fec_div_round_up(f->data_size, FEC_BLOCKSIZE);
    f->ecc.rounds = fec_div_round_up(f->ecc.blocks, f->ecc.rsn);

    if (header.fec_size !=
            (uint32_t)f->ecc.rounds * f->ecc.roots * FEC_BLOCKSIZE) {
        error("inconsistent ecc size %u", header.fec_size);
        return -1;
    }

    f->ecc.size = header.fec_size;
    f->ecc.start = header.inp_size;

    /* validate encoding data; caller may opt not to use it if invalid */
    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    uint8_t buf[FEC_BLOCKSIZE];
    uint32_t n = 0;
    uint32_t len = FEC_BLOCKSIZE;

    while (n < f->ecc.size) {
        if (len > f->ecc.size - n) {
            len = f->ecc.size - n;
        }

        if (!raw_pread(f->fd, buf, len, f->ecc.start + n)) {
            error("failed to read ecc: %s", strerror(errno));
            return -1;
        }

        SHA256_Update(&ctx, buf, len);
        n += len;
    }

    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);

    f->ecc.valid = !memcmp(hash, header.hash, SHA256_DIGEST_LENGTH);

    if (!f->ecc.valid) {
        warn("ecc data not valid");
    }

    return 0;
}

/* attempts to read an ecc header from `offset', and checks for a backup copy
   at the end of the block if the primary header is not valid */
static int parse_ecc(fec_handle *f, uint64_t offset)
{
    check(f);
    check(offset % FEC_BLOCKSIZE == 0);
    check(offset < UINT64_MAX - FEC_BLOCKSIZE);

    /* check the primary header at the beginning of the block */
    if (parse_ecc_header(f, offset) == 0) {
        return 0;
    }

    /* check the backup header at the end of the block */
    if (parse_ecc_header(f, offset + FEC_BLOCKSIZE - sizeof(fec_header)) == 0) {
        warn("using backup ecc header");
        return 0;
    }

    return -1;
}

/* reads the squashfs superblock and returns the size of the file system in
   `offset' */
static int get_squashfs_size(fec_handle *f, uint64_t *offset)
{
    check(f);
    check(offset);

    size_t sb_size = squashfs_get_sb_size();
    check(sb_size <= SSIZE_MAX);

    uint8_t buffer[sb_size];

    if (fec_pread(f, buffer, sizeof(buffer), 0) != (ssize_t)sb_size) {
        error("failed to read superblock: %s", strerror(errno));
        return -1;
    }

    squashfs_info sq;

    if (squashfs_parse_sb_buffer(buffer, &sq) < 0) {
        error("failed to parse superblock: %s", strerror(errno));
        return -1;
    }

    *offset = sq.bytes_used_4K_padded;
    return 0;
}

/* reads the ext4 superblock and returns the size of the file system in
   `offset' */
static int get_ext4_size(fec_handle *f, uint64_t *offset)
{
    check(f);
    check(f->size > 1024 + sizeof(ext4_super_block));
    check(offset);

    ext4_super_block sb;

    if (fec_pread(f, &sb, sizeof(sb), 1024) != sizeof(sb)) {
        error("failed to read superblock: %s", strerror(errno));
        return -1;
    }

    fs_info info;
    info.len = 0;  /* only len is set to 0 to ask the device for real size. */

    if (ext4_parse_sb(&sb, &info) != 0) {
        errno = EINVAL;
        return -1;
    }

    *offset = info.len;
    return 0;
}

/* attempts to determine file system size, if no fs type is specified in
   `f->flags', tries all supported types, and returns the size in `offset' */
static int get_fs_size(fec_handle *f, uint64_t *offset)
{
    check(f);
    check(offset);

    if (f->flags & FEC_FS_EXT4) {
        return get_ext4_size(f, offset);
    } else if (f->flags & FEC_FS_SQUASH) {
        return get_squashfs_size(f, offset);
    } else {
        /* try all alternatives */
        int rc = get_ext4_size(f, offset);

        if (rc == 0) {
            debug("found ext4fs");
            return rc;
        }

        rc = get_squashfs_size(f, offset);

        if (rc == 0) {
            debug("found squashfs");
        }

        return rc;
    }
}

/* locates, validates, and loads verity metadata from `f->fd' */
static int load_verity(fec_handle *f)
{
    check(f);
    debug("size = %" PRIu64 ", flags = %d", f->data_size, f->flags);

    uint64_t offset = f->data_size - VERITY_METADATA_SIZE;

    /* verity header is at the end of the data area */
    if (verity_parse_header(f, offset) == 0) {
        debug("found at %" PRIu64 " (start %" PRIu64 ")", offset,
              f->verity.hashtree.hash_start);
        return 0;
    }

    debug("trying legacy formats");

    /* legacy format at the end of the partition */
    if (find_verity_offset(f, &offset) == 0 &&
            verity_parse_header(f, offset) == 0) {
        debug("found at %" PRIu64 " (start %" PRIu64 ")", offset,
              f->verity.hashtree.hash_start);
        return 0;
    }

    /* legacy format after the file system, but not at the end */
    int rc = get_fs_size(f, &offset);
    if (rc == 0) {
        debug("file system size = %" PRIu64, offset);
        /* Jump over the verity tree appended to the filesystem */
        offset += verity_get_size(offset, NULL, NULL, SHA256_DIGEST_LENGTH);
        rc = verity_parse_header(f, offset);

        if (rc == 0) {
            debug("found at %" PRIu64 " (start %" PRIu64 ")", offset,
                  f->verity.hashtree.hash_start);
        }
    }

    return rc;
}

/* locates, validates, and loads ecc data from `f->fd' */
static int load_ecc(fec_handle *f)
{
    check(f);
    debug("size = %" PRIu64, f->data_size);

    uint64_t offset = f->data_size - FEC_BLOCKSIZE;

    if (parse_ecc(f, offset) == 0) {
        debug("found at %" PRIu64 " (start %" PRIu64 ")", offset,
            f->ecc.start);
        return 0;
    }

    return -1;
}

/* sets `f->size' to the size of the file or block device */
static int get_size(fec_handle *f)
{
    check(f);

    struct stat st;

    if (fstat(f->fd, &st) == -1) {
        error("fstat failed: %s", strerror(errno));
        return -1;
    }

    if (S_ISBLK(st.st_mode)) {
        debug("block device");

        if (ioctl(f->fd, BLKGETSIZE64, &f->size) == -1) {
            error("ioctl failed: %s", strerror(errno));
            return -1;
        }
    } else if (S_ISREG(st.st_mode)) {
        debug("file");
        f->size = st.st_size;
    } else {
        error("unsupported type %d", (int)st.st_mode);
        errno = EACCES;
        return -1;
    }

    return 0;
}

/* clears fec_handle fiels to safe values */
static void reset_handle(fec_handle *f)
{
    f->fd = -1;
    f->flags = 0;
    f->mode = 0;
    f->errors = 0;
    f->data_size = 0;
    f->pos = 0;
    f->size = 0;

    f->ecc = {};
    f->verity = {};
}

/* closes and flushes `f->fd' and releases any memory allocated for `f' */
int fec_close(struct fec_handle *f)
{
    check(f);

    if (f->fd != -1) {
        if (f->mode & O_RDWR && fdatasync(f->fd) == -1) {
            warn("fdatasync failed: %s", strerror(errno));
        }

        close(f->fd);
    }

    reset_handle(f);
    delete f;

    return 0;
}

/* populates `data' from the internal data in `f', returns a value <0 if verity
   metadata is not available in `f->fd' */
int fec_verity_get_metadata(struct fec_handle *f, struct fec_verity_metadata *data)
{
    check(f);
    check(data);

    if (!f->verity.metadata_start) {
        return -1;
    }

    check(f->data_size < f->size);
    check(f->data_size <= f->verity.hashtree.hash_start);
    check(f->data_size <= f->verity.metadata_start);
    check(!f->verity.table.empty());

    data->disabled = f->verity.disabled;
    data->data_size = f->data_size;
    memcpy(data->signature, f->verity.header.signature,
        sizeof(data->signature));
    memcpy(data->ecc_signature, f->verity.ecc_header.signature,
        sizeof(data->ecc_signature));
    data->table = f->verity.table.c_str();
    data->table_length = f->verity.header.length;

    return 0;
}

/* populates `data' from the internal data in `f', returns a value <0 if ecc
   metadata is not available in `f->fd' */
int fec_ecc_get_metadata(struct fec_handle *f, struct fec_ecc_metadata *data)
{
    check(f);
    check(data);

    if (!f->ecc.start) {
        return -1;
    }

    check(f->data_size < f->size);
    check(f->ecc.start >= f->data_size);
    check(f->ecc.start < f->size);
    check(f->ecc.start % FEC_BLOCKSIZE == 0);

    data->valid = f->ecc.valid;
    data->roots = f->ecc.roots;
    data->blocks = f->ecc.blocks;
    data->rounds = f->ecc.rounds;
    data->start = f->ecc.start;

    return 0;
}

/* populates `data' from the internal status in `f' */
int fec_get_status(struct fec_handle *f, struct fec_status *s)
{
    check(f);
    check(s);

    s->flags = f->flags;
    s->mode = f->mode;
    s->errors = f->errors;
    s->data_size = f->data_size;
    s->size = f->size;

    return 0;
}

/* opens `path' using given options and returns a fec_handle in `handle' if
   successful */
int fec_open(struct fec_handle **handle, const char *path, int mode, int flags,
        int roots)
{
    check(path);
    check(handle);
    check(roots > 0 && roots < FEC_RSM);

    debug("path = %s, mode = %d, flags = %d, roots = %d", path, mode, flags,
        roots);

    if (mode & (O_CREAT | O_TRUNC | O_EXCL | O_WRONLY)) {
        /* only reading and updating existing files is supported */
        error("failed to open '%s': (unsupported mode %d)", path, mode);
        errno = EACCES;
        return -1;
    }

    fec::handle f(new (std::nothrow) fec_handle, fec_close);

    if (unlikely(!f)) {
        error("failed to allocate file handle");
        errno = ENOMEM;
        return -1;
    }

    reset_handle(f.get());

    f->mode = mode;
    f->ecc.roots = roots;
    f->ecc.rsn = FEC_RSM - roots;
    f->flags = flags;

    f->fd = TEMP_FAILURE_RETRY(open(path, mode | O_CLOEXEC));

    if (f->fd == -1) {
        error("failed to open '%s': %s", path, strerror(errno));
        return -1;
    }

    if (get_size(f.get()) == -1) {
        error("failed to get size for '%s': %s", path, strerror(errno));
        return -1;
    }

    f->data_size = f->size; /* until ecc and/or verity are loaded */

    // Don't parse the avb image if FEC_NO_AVB is set. It's used when libavb is
    // not supported on mac.
    std::vector<uint8_t> vbmeta;
    if (parse_vbmeta_from_footer(f.get(), &vbmeta) == 0) {
        if (parse_avb_image(f.get(), vbmeta) != 0) {
            error("failed to parse avb image.");
            return -1;
        }

        *handle = f.release();
        return 0;
    }
    // TODO(xunchang) For android, handle the case when vbmeta is in a separate
    // image. We could use avb_slot_verify() && AvbOps from libavb_user.

    // Fall back to use verity format.

    if (load_ecc(f.get()) == -1) {
        debug("error-correcting codes not found from '%s'", path);
    }

    if (load_verity(f.get()) == -1) {
        debug("verity metadata not found from '%s'", path);
    }

    *handle = f.release();
    return 0;
}
