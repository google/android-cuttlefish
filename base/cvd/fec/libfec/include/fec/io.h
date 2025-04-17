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

#ifndef ___FEC_IO_H___
#define ___FEC_IO_H___

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include <crypto_utils/android_pubkey.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SHA256_DIGEST_LENGTH
#define SHA256_DIGEST_LENGTH 32
#endif

#define FEC_BLOCKSIZE 4096
#define FEC_DEFAULT_ROOTS 2

#define FEC_MAGIC 0xFECFECFE
#define FEC_VERSION 0

/* disk format for the header */
struct fec_header {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t roots;
    uint32_t fec_size;
    uint64_t inp_size;
    uint8_t hash[SHA256_DIGEST_LENGTH];
} __attribute__ ((packed));

struct fec_status {
    int flags;
    int mode;
    uint64_t errors;
    uint64_t data_size;
    uint64_t size;
};

struct fec_ecc_metadata {
    bool valid;
    uint32_t roots;
    uint64_t blocks;
    uint64_t rounds;
    uint64_t start;
};

struct fec_verity_metadata {
    bool disabled;
    uint64_t data_size;
    uint8_t signature[ANDROID_PUBKEY_MODULUS_SIZE];
    uint8_t ecc_signature[ANDROID_PUBKEY_MODULUS_SIZE];
    const char *table;
    uint32_t table_length;
};

/* flags for fec_open */
enum {
    FEC_FS_EXT4 = 1 << 0,
    FEC_FS_SQUASH = 1 << 1,
    FEC_VERITY_DISABLE = 1 << 8
};

struct fec_handle;

/* file access */
extern int fec_open(struct fec_handle **f, const char *path, int mode,
        int flags, int roots);

extern int fec_close(struct fec_handle *f);

extern int fec_verity_set_status(struct fec_handle *f, bool enabled);

extern int fec_verity_get_metadata(struct fec_handle *f,
        struct fec_verity_metadata *data);

extern int fec_ecc_get_metadata(struct fec_handle *f,
        struct fec_ecc_metadata *data);

extern int fec_get_status(struct fec_handle *f, struct fec_status *s);

extern int fec_seek(struct fec_handle *f, int64_t offset, int whence);

extern ssize_t fec_read(struct fec_handle *f, void *buf, size_t count);

extern ssize_t fec_pread(struct fec_handle *f, void *buf, size_t count,
        uint64_t offset);

#ifdef __cplusplus
} /* extern "C" */

#include <memory>
#include <string>

/* C++ wrappers for fec_handle and operations */
namespace fec {
    using handle = std::unique_ptr<fec_handle, decltype(&fec_close)>;

    class io {
    public:
        io() : handle_(nullptr, fec_close) {}

        explicit io(const std::string& fn, int mode = O_RDONLY, int flags = 0,
                int roots = FEC_DEFAULT_ROOTS) : handle_(nullptr, fec_close) {
            open(fn, mode, flags, roots);
        }

        explicit operator bool() const {
            return !!handle_;
        }

        bool open(const std::string& fn, int mode = O_RDONLY, int flags = 0,
                    int roots = FEC_DEFAULT_ROOTS)
        {
            fec_handle *fh = nullptr;
            int rc = fec_open(&fh, fn.c_str(), mode, flags, roots);
            if (!rc) {
                handle_.reset(fh);
            }
            return !rc;
        }

        bool close() {
            return !fec_close(handle_.release());
        }

        bool seek(int64_t offset, int whence) {
            return !fec_seek(handle_.get(), offset, whence);
        }

        ssize_t read(void *buf, size_t count) {
            return fec_read(handle_.get(), buf, count);
        }

        ssize_t pread(void *buf, size_t count, uint64_t offset) {
            return fec_pread(handle_.get(), buf, count, offset);
        }

        bool get_status(fec_status& status) {
            return !fec_get_status(handle_.get(), &status);
        }

        bool get_verity_metadata(fec_verity_metadata& data) {
            return !fec_verity_get_metadata(handle_.get(), &data);
        }

        bool has_verity() {
            fec_verity_metadata data;
            return get_verity_metadata(data);
        }

        bool get_ecc_metadata(fec_ecc_metadata& data) {
            return !fec_ecc_get_metadata(handle_.get(), &data);
        }

        bool has_ecc() {
            fec_ecc_metadata data;
            return get_ecc_metadata(data) && data.valid;
        }

        bool set_verity_status(bool enabled) {
            return !fec_verity_set_status(handle_.get(), enabled);
        }

    private:
        handle handle_;
    };
}
#endif

#endif /* ___FEC_IO_H___ */
