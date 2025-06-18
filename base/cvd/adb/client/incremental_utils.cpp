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
#include <ziparchive/zip_archive.h>
#include <ziparchive/zip_writer.h>

#include "adb_io.h"
#include "adb_trace.h"
#include "sysdeps.h"

using namespace std::literals;

namespace incremental {

static constexpr inline int32_t offsetToBlockIndex(int64_t offset) {
    return (offset & ~(kBlockSize - 1)) >> 12;
}

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

static void appendBlocks(int32_t start, int count, std::vector<int32_t>* blocks) {
    if (count == 1) {
        blocks->push_back(start);
    } else {
        auto oldSize = blocks->size();
        blocks->resize(oldSize + count);
        std::iota(blocks->begin() + oldSize, blocks->end(), start);
    }
}

template <class T>
static void unduplicate(std::vector<T>& v) {
    std::unordered_set<T> uniques(v.size());
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&uniques](T t) { return !uniques.insert(t).second; }),
            v.end());
}

static off64_t CentralDirOffset(borrowed_fd fd, Size fileSize) {
    static constexpr int kZipEocdRecMinSize = 22;
    static constexpr int32_t kZipEocdRecSig = 0x06054b50;
    static constexpr int kZipEocdCentralDirSizeFieldOffset = 12;
    static constexpr int kZipEocdCommentLengthFieldOffset = 20;

    int32_t sigBuf = 0;
    off64_t eocdOffset = -1;
    off64_t maxEocdOffset = fileSize - kZipEocdRecMinSize;
    int16_t commentLenBuf = 0;

    // Search from the end of zip, backward to find beginning of EOCD
    for (int16_t commentLen = 0; commentLen < fileSize; ++commentLen) {
        sigBuf = valueAt<int32_t>(fd, maxEocdOffset - commentLen);
        if (sigBuf == kZipEocdRecSig) {
            commentLenBuf = valueAt<int16_t>(
                    fd, maxEocdOffset - commentLen + kZipEocdCommentLengthFieldOffset);
            if (commentLenBuf == commentLen) {
                eocdOffset = maxEocdOffset - commentLen;
                break;
            }
        }
    }

    if (eocdOffset < 0) {
        return -1;
    }

    off64_t cdLen = static_cast<int64_t>(
            valueAt<int32_t>(fd, eocdOffset + kZipEocdCentralDirSizeFieldOffset));

    return eocdOffset - cdLen;
}

// Does not support APKs larger than 4GB
static off64_t SignerBlockOffset(borrowed_fd fd, Size fileSize) {
    static constexpr int kApkSigBlockMinSize = 32;
    static constexpr int kApkSigBlockFooterSize = 24;
    static constexpr int64_t APK_SIG_BLOCK_MAGIC_HI = 0x3234206b636f6c42l;
    static constexpr int64_t APK_SIG_BLOCK_MAGIC_LO = 0x20676953204b5041l;

    off64_t cdOffset = CentralDirOffset(fd, fileSize);
    if (cdOffset < 0) {
        return -1;
    }
    // CD offset is where original signer block ends. Search backwards for magic and footer.
    if (cdOffset < kApkSigBlockMinSize ||
        valueAt<int64_t>(fd, cdOffset - 2 * sizeof(int64_t)) != APK_SIG_BLOCK_MAGIC_LO ||
        valueAt<int64_t>(fd, cdOffset - sizeof(int64_t)) != APK_SIG_BLOCK_MAGIC_HI) {
        return -1;
    }
    int32_t signerSizeInFooter = valueAt<int32_t>(fd, cdOffset - kApkSigBlockFooterSize);
    off64_t signerBlockOffset = cdOffset - signerSizeInFooter - sizeof(int64_t);
    if (signerBlockOffset < 0) {
        return -1;
    }
    int32_t signerSizeInHeader = valueAt<int32_t>(fd, signerBlockOffset);
    if (signerSizeInFooter != signerSizeInHeader) {
        return -1;
    }

    return signerBlockOffset;
}

static std::vector<int32_t> ZipPriorityBlocks(off64_t signerBlockOffset, Size fileSize) {
    int32_t signerBlockIndex = offsetToBlockIndex(signerBlockOffset);
    int32_t lastBlockIndex = offsetToBlockIndex(fileSize);
    const auto numPriorityBlocks = lastBlockIndex - signerBlockIndex + 1;

    std::vector<int32_t> zipPriorityBlocks;

    // Some magic here: most of zip libraries perform a scan for EOCD record starting at the offset
    // of a maximum comment size from the end of the file. This means the last 65-ish KBs will be
    // accessed first, followed by the rest of the central directory blocks. Make sure we
    // send the data in the proper order, as central directory can be quite big by itself.
    static constexpr auto kMaxZipCommentSize = 64 * 1024;
    static constexpr auto kNumBlocksInEocdSearch = kMaxZipCommentSize / kBlockSize + 1;
    if (numPriorityBlocks > kNumBlocksInEocdSearch) {
        appendBlocks(lastBlockIndex - kNumBlocksInEocdSearch + 1, kNumBlocksInEocdSearch,
                     &zipPriorityBlocks);
        appendBlocks(signerBlockIndex, numPriorityBlocks - kNumBlocksInEocdSearch,
                     &zipPriorityBlocks);
    } else {
        appendBlocks(signerBlockIndex, numPriorityBlocks, &zipPriorityBlocks);
    }

    // Somehow someone keeps accessing the start of the archive, even if there's nothing really
    // interesting there...
    appendBlocks(0, 1, &zipPriorityBlocks);
    return zipPriorityBlocks;
}

[[maybe_unused]] static ZipArchiveHandle openZipArchiveFd(borrowed_fd fd) {
    bool transferFdOwnership = false;
#ifdef _WIN32
    //
    // Need to create a special CRT FD here as the current one is not compatible with
    // normal read()/write() calls that libziparchive uses.
    // To make this work we have to create a copy of the file handle, as CRT doesn't care
    // and closes it together with the new descriptor.
    //
    // Note: don't move this into a helper function, it's better to be hard to reuse because
    //       the code is ugly and won't work unless it's a last resort.
    //
    auto handle = adb_get_os_handle(fd);
    HANDLE dupedHandle;
    if (!::DuplicateHandle(::GetCurrentProcess(), handle, ::GetCurrentProcess(), &dupedHandle, 0,
                           false, DUPLICATE_SAME_ACCESS)) {
        D("%s failed at DuplicateHandle: %d", __func__, (int)::GetLastError());
        return {};
    }
    int osfd = _open_osfhandle((intptr_t)dupedHandle, _O_RDONLY | _O_BINARY);
    if (osfd < 0) {
        D("%s failed at _open_osfhandle: %d", __func__, errno);
        ::CloseHandle(handle);
        return {};
    }
    transferFdOwnership = true;
#else
    int osfd = fd.get();
#endif
    ZipArchiveHandle zip;
    if (OpenArchiveFd(osfd, "apk_fd", &zip, transferFdOwnership) != 0) {
        D("%s failed at OpenArchiveFd: %d", __func__, errno);
#ifdef _WIN32
        // "_close()" is a secret WinCRT name for the regular close() function.
        _close(osfd);
#endif
        return {};
    }
    return zip;
}

static std::pair<ZipArchiveHandle, std::unique_ptr<android::base::MappedFile>> openZipArchive(
        borrowed_fd fd, Size fileSize) {
#ifndef __LP64__
    if (fileSize >= INT_MAX) {
        return {openZipArchiveFd(fd), nullptr};
    }
#endif
    auto mapping =
            android::base::MappedFile::FromOsHandle(adb_get_os_handle(fd), 0, fileSize, PROT_READ);
    if (!mapping) {
        D("%s failed at FromOsHandle: %d", __func__, errno);
        return {};
    }
    ZipArchiveHandle zip;
    if (OpenArchiveFromMemory(mapping->data(), mapping->size(), "apk_mapping", &zip) != 0) {
        D("%s failed at OpenArchiveFromMemory: %d", __func__, errno);
        return {};
    }
    return {zip, std::move(mapping)};
}

static std::vector<int32_t> InstallationPriorityBlocks(borrowed_fd fd, Size fileSize) {
    static constexpr std::array<std::string_view, 3> additional_matches = {
            "resources.arsc"sv, "AndroidManifest.xml"sv, "classes.dex"sv};
    auto [zip, _] = openZipArchive(fd, fileSize);
    if (!zip) {
        return {};
    }

    auto matcher = [](std::string_view entry_name) {
        if (entry_name.starts_with("lib/"sv) && entry_name.ends_with(".so"sv)) {
            return true;
        }
        return std::any_of(additional_matches.begin(), additional_matches.end(),
                           [entry_name](std::string_view i) { return i == entry_name; });
    };

    void* cookie = nullptr;
    if (StartIteration(zip, &cookie, std::move(matcher)) != 0) {
        D("%s failed at StartIteration: %d", __func__, errno);
        return {};
    }

    std::vector<int32_t> installationPriorityBlocks;
    ZipEntry64 entry;
    std::string_view entryName;
    while (Next(cookie, &entry, &entryName) == 0) {
        if (entryName == "classes.dex"sv) {
            // Only the head is needed for installation
            int32_t startBlockIndex = offsetToBlockIndex(entry.offset);
            appendBlocks(startBlockIndex, 2, &installationPriorityBlocks);
            D("\tadding to priority blocks: '%.*s' (%d)", (int)entryName.size(), entryName.data(),
              2);
        } else {
            // Full entries are needed for installation
            off64_t entryStartOffset = entry.offset;
            off64_t entryEndOffset =
                    entryStartOffset +
                    (entry.method == kCompressStored ? entry.uncompressed_length
                                                     : entry.compressed_length) +
                    (entry.has_data_descriptor ? 16 /* sizeof(DataDescriptor) */ : 0);
            int32_t startBlockIndex = offsetToBlockIndex(entryStartOffset);
            int32_t endBlockIndex = offsetToBlockIndex(entryEndOffset);
            int32_t numNewBlocks = endBlockIndex - startBlockIndex + 1;
            appendBlocks(startBlockIndex, numNewBlocks, &installationPriorityBlocks);
            D("\tadding to priority blocks: '%.*s' (%d)", (int)entryName.size(), entryName.data(),
              numNewBlocks);
        }
    }

    EndIteration(cookie);
    CloseArchive(zip);
    return installationPriorityBlocks;
}

std::vector<int32_t> PriorityBlocksForFile(const std::string& filepath, borrowed_fd fd,
                                           Size fileSize) {
    if (!android::base::EndsWithIgnoreCase(filepath, ".apk"sv)) {
        return {};
    }
    off64_t signerOffset = SignerBlockOffset(fd, fileSize);
    if (signerOffset < 0) {
        // No signer block? not a valid APK
        return {};
    }
    std::vector<int32_t> priorityBlocks = ZipPriorityBlocks(signerOffset, fileSize);
    std::vector<int32_t> installationPriorityBlocks = InstallationPriorityBlocks(fd, fileSize);

    priorityBlocks.insert(priorityBlocks.end(), installationPriorityBlocks.begin(),
                          installationPriorityBlocks.end());
    unduplicate(priorityBlocks);
    return priorityBlocks;
}

}  // namespace incremental
