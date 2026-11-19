/*
 * Copyright (C) 2013 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "util.h"

using android::base::borrowed_fd;

static bool g_verbose = false;

double now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000;
}

void die(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "fastboot: error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(EXIT_FAILURE);
}

void die(const std::string& str) {
    die("%s", str.c_str());
}

void set_verbose() {
    g_verbose = true;
}

void verbose(const char* fmt, ...) {
    if (!g_verbose) return;

    if (*fmt != '\n') {
        va_list ap;
        va_start(ap, fmt);
        fprintf(stderr, "fastboot: verbose: ");
        vfprintf(stderr, fmt, ap);
        va_end(ap);
    }
    fprintf(stderr, "\n");
}

bool should_flash_in_userspace(const android::fs_mgr::LpMetadata& metadata,
                               const std::string& partition_name) {
    for (const auto& partition : metadata.partitions) {
        auto candidate = android::fs_mgr::GetPartitionName(partition);
        if (partition.attributes & LP_PARTITION_ATTR_SLOT_SUFFIXED) {
            // On retrofit devices, we don't know if, or whether, the A or B
            // slot has been flashed for dynamic partitions. Instead we add
            // both names to the list as a conservative guess.
            if (candidate + "_a" == partition_name || candidate + "_b" == partition_name) {
                return true;
            }
        } else if (candidate == partition_name) {
            return true;
        }
    }
    return false;
}

bool is_sparse_file(borrowed_fd fd) {
    SparsePtr s(sparse_file_import(fd.get(), false, false), sparse_file_destroy);
    return !!s;
}

int64_t get_file_size(borrowed_fd fd) {
    struct stat sb;
    if (fstat(fd.get(), &sb) == -1) {
        die("could not get file size");
    }
    return sb.st_size;
}

std::string fb_fix_numeric_var(std::string var) {
    // Some bootloaders (angler, for example), send spurious leading whitespace.
    var = android::base::Trim(var);
    // Some bootloaders (hammerhead, for example) use implicit hex.
    // This code used to use strtol with base 16.
    if (!android::base::StartsWith(var, "0x")) var = "0x" + var;
    return var;
}

bool split_file(int fd, int64_t max_size, std::vector<SparsePtr>* out) {
    if (max_size < 0 || max_size > std::numeric_limits<uint32_t>::max()) {
        LOG(ERROR) << "invalid max size: " << max_size;
        return false;
    }

    SparsePtr s(sparse_file_import(fd, true, false), sparse_file_destroy);
    if (!s) {
        int64_t size = get_file_size(fd);
        int block_size;
        if (size % 4096 == 0) {
            block_size = 4096;
        } else if (size % 512 == 0) {
            block_size = 512;
        } else {
            // Resparsing this file will fail, since it is unaligned. libsparse
            // will either infinite loop or error out.
            LOG(ERROR) << "File size " << size
                       << " is unaligned, and greater than the maximum download size " << max_size
                       << ", unable to resparse for transfer.";
            return false;
        }

        if (lseek(fd, 0, SEEK_SET) < 0) {
            PLOG(ERROR) << "lseek failed";
            return false;
        }

        s = SparsePtr(sparse_file_new(block_size, size), sparse_file_destroy);
        if (!s || sparse_file_read(s.get(), fd, SPARSE_READ_MODE_NORMAL, false) < 0) {
            LOG(ERROR) << "Failed to read file into sparse buffer";
            return false;
        }
    }

    // We're now guaranteed to have a sparse file. If the sparse len < download size, don't split.
    int64_t sparse_len = sparse_file_len(s.get(), false, false);
    if (sparse_len <= max_size) {
        out->emplace_back(std::move(s));
        return true;
    }

    return split_file(s.get(), max_size, out);
}

bool split_file(sparse_file* s, int64_t max_size, std::vector<SparsePtr>* out) {
    const int files = sparse_file_resparse(s, max_size, nullptr, 0);
    if (files < 0) {
        LOG(ERROR) << "Failed to compute resparse boundaries";
        return false;
    }

    auto temp = std::make_unique<sparse_file*[]>(files);
    const int rv = sparse_file_resparse(s, max_size, temp.get(), files);
    if (rv < 0) {
        LOG(ERROR) << "Failed to resparse";
        return false;
    }

    for (int i = 0; i < files; i++) {
        out->emplace_back(temp[i], sparse_file_destroy);
    }
    return true;
}
