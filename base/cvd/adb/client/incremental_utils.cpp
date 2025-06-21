/*
 * Copyright (C) 2020 The Android Open Source Project
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

#define TRACE_TAG INCREMENTAL

#include "incremental_utils.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <numeric>
#include <optional>
#include <unordered_set>
#include <utility>

#include <android-base/endian.h>
#include <android-base/mapped_file.h>
#include <android-base/strings.h>

#include "adb_io.h"
#include "adb_trace.h"
#include "sysdeps.h"

using namespace std::literals;

namespace incremental {

Size verity_tree_blocks_for_file(Size fileSize) {
    if (fileSize == 0) {
        return 0;
    }

    constexpr int hash_per_block = kBlockSize / kDigestSize;

    Size total_tree_block_count = 0;

    const Size block_count = 1 + (fileSize - 1) / kBlockSize;
    Size hash_block_count = block_count;
    while (hash_block_count > 1) {
        hash_block_count = (hash_block_count + hash_per_block - 1) / hash_per_block;
        total_tree_block_count += hash_block_count;
    }
    return total_tree_block_count;
}

Size verity_tree_size_for_file(Size fileSize) {
    return verity_tree_blocks_for_file(fileSize) * kBlockSize;
}

static inline std::optional<int32_t> read_int32(borrowed_fd fd, std::string* error) {
    int32_t result;
    if (!ReadFdExactly(fd, &result, sizeof(result))) {
        *error =
                std::format("Failed to read int: {}", errno == 0 ? "End of file" : strerror(errno));
        return {};
    }
    return result;
}

static inline bool skip_int(borrowed_fd fd, std::string* error) {
    if (adb_lseek(fd, 4, SEEK_CUR) < 0) {
        *error = std::format("Failed to seek: {}", strerror(errno));
        return false;
    }
    return true;
}

static inline bool append_int(borrowed_fd fd, std::vector<char>* bytes, std::string* error) {
    std::optional<int32_t> le_val = read_int32(fd, error);
    if (!le_val.has_value()) {
        return false;
    }
    auto old_size = bytes->size();
    bytes->resize(old_size + sizeof(*le_val));
    memcpy(bytes->data() + old_size, &le_val.value(), sizeof(*le_val));
    return true;
}

static inline bool append_bytes_with_size(borrowed_fd fd, std::vector<char>* bytes, int* bytes_left,
                                          std::string* error) {
    std::optional<int32_t> le_size = read_int32(fd, error);
    if (!le_size.has_value()) {
        return false;
    }
    int32_t size = int32_t(le32toh(*le_size));
    if (size < 0 || size > *bytes_left) {
        *error = std::format("Invalid size {}", size);
        return false;
    }
    if (size == 0) {
        return true;
    }
    *bytes_left -= size;
    auto old_size = bytes->size();
    bytes->resize(old_size + sizeof(*le_size) + size);
    memcpy(bytes->data() + old_size, &le_size.value(), sizeof(*le_size));
    if (!ReadFdExactly(fd, bytes->data() + old_size + sizeof(*le_size), size)) {
        *error = std::format("Failed to read data: {}",
                             errno == 0 ? "End of file" : strerror(errno));
        return false;
    }
    return true;
}

static inline bool skip_bytes_with_size(borrowed_fd fd, std::string* error) {
    std::optional<int32_t> le_size = read_int32(fd, error);
    if (!le_size.has_value()) {
        return false;
    }
    int32_t size = int32_t(le32toh(*le_size));
    if (size < 0) {
        *error = std::format("Invalid sze {}", size);
        return false;
    }
    if (adb_lseek(fd, size, SEEK_CUR) < 0) {
        *error = "Failed to seek";
        return false;
    }
    return true;
}

std::optional<std::pair<std::vector<char>, int32_t>> read_id_sig_headers(borrowed_fd fd,
                                                                         std::string* error) {
    std::vector<char> signature;
    if (!append_int(fd, &signature, error)) {  // version
        return {};
    }
    int max_size = kMaxSignatureSize - sizeof(int32_t);
    if (!append_bytes_with_size(fd, &signature, &max_size, error)) {  // hashingInfo
        return {};
    }
    if (!append_bytes_with_size(fd, &signature, &max_size, error)) {  // signingInfo
        return {};
    }
    std::optional<int32_t> le_tree_size = read_int32(fd, error);
    if (!le_tree_size.has_value()) {
        return {};
    }
    auto tree_size = int32_t(le32toh(*le_tree_size));  // size of the verity tree
    return std::make_pair(std::move(signature), tree_size);
}

std::optional<std::pair<off64_t, ssize_t>> skip_id_sig_headers(borrowed_fd fd, std::string* error) {
    if (!skip_int(fd, error)) {  // version
        return {};
    }
    if (!skip_bytes_with_size(fd, error)) {  // hashingInfo
        return {};
    }
    if (!skip_bytes_with_size(fd, error)) {  // signingInfo
        return {};
    }
    std::optional<int32_t> le_tree_size = read_int32(fd, error);
    if (!le_tree_size.has_value()) {
        return {};
    }
    int32_t tree_size = int32_t(le32toh(*le_tree_size));  // size of the verity tree
    off_t offset = adb_lseek(fd, 0, SEEK_CUR);
    if (offset < 0) {
        *error = std::format("Failed to get offset: {}", strerror(errno));
        return {};
    }
    return std::make_pair(offset, tree_size);
}

template <class T>
static T valueAt(borrowed_fd fd, off64_t offset) {
    T t;
    memset(&t, 0, sizeof(T));
    if (adb_pread(fd, &t, sizeof(T), offset) != sizeof(T)) {
        memset(&t, -1, sizeof(T));
    }

    return t;
}

template <class T>
static void unduplicate(std::vector<T>& v) {
    std::unordered_set<T> uniques(v.size());
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&uniques](T t) { return !uniques.insert(t).second; }),
            v.end());
}

std::vector<int32_t> PriorityBlocksForFile(const std::string&, borrowed_fd,
                                           Size) {
    return {};
}

}  // namespace incremental
