/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Read-only access to Zip archives, with minimal heap allocation.
 */

#define LOG_TAG "ziparchive"

#include "ziparchive/zip_archive.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#endif

#include <memory>
#include <optional>
#include <span>
#include <vector>

#if defined(__APPLE__)
#define lseek64 lseek
#endif

#if defined(__BIONIC__)
#include <android/fdsan.h>
#endif

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/macros.h>  // TEMP_FAILURE_RETRY may or may not be in unistd
#include <android-base/mapped_file.h>
#include <android-base/memory.h>
#include <android-base/strings.h>
#include <android-base/utf8.h>
#include <log/log.h>

#include "entry_name_utils-inl.h"
#include "incfs_support/signal_handling.h"
#include "incfs_support/util.h"
#include "zip_archive_common.h"
#include "zip_archive_private.h"
#include "zlib.h"

// Used to turn on crc checks - verify that the content CRC matches the values
// specified in the local file header and the central directory.
static constexpr bool kCrcChecksEnabled = false;

// The maximum number of bytes to scan backwards for the EOCD start.
static const uint32_t kMaxEOCDSearch = kMaxCommentLen + sizeof(EocdRecord);

// Set a reasonable cap (256 GiB) for the zip file size. So the data is always valid when
// we parse the fields in cd or local headers as 64 bits signed integers.
static constexpr uint64_t kMaxFileLength = 256 * static_cast<uint64_t>(1u << 30u);

/*
 * A Read-only Zip archive.
 *
 * We want "open" and "find entry by name" to be fast operations, and
 * we want to use as little memory as possible.  We memory-map the zip
 * central directory, and load a hash table with pointers to the filenames
 * (which aren't null-terminated).  The other fields are at a fixed offset
 * from the filename, so we don't need to extract those (but we do need
 * to byte-read and endian-swap them every time we want them).
 *
 * It's possible that somebody has handed us a massive (~1GB) zip archive,
 * so we can't expect to mmap the entire file.
 *
 * To speed comparisons when doing a lookup by name, we could make the mapping
 * "private" (copy-on-write) and null-terminate the filenames after verifying
 * the record structure.  However, this requires a private mapping of
 * every page that the Central Directory touches.  Easier to tuck a copy
 * of the string length into the hash table entry.
 */

#ifdef __linux__
static const size_t kPageSize = getpagesize();
#else
constexpr size_t kPageSize = 4096;
#endif

[[maybe_unused]] static uintptr_t pageAlignDown(uintptr_t ptr_int) {
  return ptr_int & ~(kPageSize - 1);
}

[[maybe_unused]] static uintptr_t pageAlignUp(uintptr_t ptr_int) {
  return pageAlignDown(ptr_int + kPageSize - 1);
}

[[maybe_unused]] static std::pair<void*, size_t> expandToPageBounds(void* ptr, size_t size) {
  const auto ptr_int = reinterpret_cast<uintptr_t>(ptr);
  const auto aligned_ptr_int = pageAlignDown(ptr_int);
  const auto aligned_size = pageAlignUp(ptr_int + size) - aligned_ptr_int;
  return {reinterpret_cast<void*>(aligned_ptr_int), aligned_size};
}

[[maybe_unused]] static void maybePrefetch([[maybe_unused]] const void* ptr,
                                           [[maybe_unused]] size_t size) {
#ifdef __linux__
  // Let's only ask for a readahead explicitly if there's enough pages to read. A regular OS
  // readahead implementation would take care of the smaller requests, and it would also involve
  // only a single kernel transition, just an implicit one from the page fault.
  //
  // Note: there's no implementation for other OSes, as the prefetch logic is highly specific
  // to the memory manager, and we don't have any well defined benchmarks on Windows/Mac;
  // it also mostly matters only for the cold OS boot where no files are in the page cache yet,
  // but we rarely would hit this situation outside of the device startup.
  auto [aligned_ptr, aligned_size] = expandToPageBounds(const_cast<void*>(ptr), size);
  if (aligned_size > 32 * kPageSize) {
    if (::madvise(aligned_ptr, aligned_size, MADV_WILLNEED)) {
      ALOGW("Zip: madvise(file, WILLNEED) failed: %s (%d)", strerror(errno), errno);
    }
  }
#endif
}

[[maybe_unused]] static void maybePrepareSequentialReading([[maybe_unused]] const void* ptr,
                                                           [[maybe_unused]] size_t size) {
#ifdef __linux__
  auto [aligned_ptr, aligned_size] = expandToPageBounds(const_cast<void*>(ptr), size);
  if (::madvise(reinterpret_cast<void*>(aligned_ptr), aligned_size, MADV_SEQUENTIAL)) {
    ALOGW("Zip: madvise(file, SEQUENTIAL) failed: %s (%d)", strerror(errno), errno);
  }
#endif
}

#if defined(__BIONIC__)
static uint64_t GetOwnerTag(const ZipArchive* archive) {
  return android_fdsan_create_owner_tag(ANDROID_FDSAN_OWNER_TYPE_ZIPARCHIVE,
                                        reinterpret_cast<uint64_t>(archive));
}
#endif

ZipArchive::ZipArchive(MappedZipFile&& map, bool assume_ownership)
    : mapped_zip(std::move(map)),
      close_file(assume_ownership),
      directory_offset(0),
      central_directory(),
      directory_map(),
      num_entries(0) {
#if defined(__BIONIC__)
  if (assume_ownership) {
    CHECK(mapped_zip.GetFileDescriptor() >= 0 || !mapped_zip.GetBasePtr());
    android_fdsan_exchange_owner_tag(mapped_zip.GetFileDescriptor(), 0, GetOwnerTag(this));
  }
#endif
}

ZipArchive::ZipArchive(const void* address, size_t length)
    : mapped_zip(address, length),
      close_file(false),
      directory_offset(0),
      central_directory(),
      directory_map(),
      num_entries(0) {}

ZipArchive::~ZipArchive() {
  if (close_file && mapped_zip.GetFileDescriptor() >= 0) {
#if defined(__BIONIC__)
    android_fdsan_close_with_tag(mapped_zip.GetFileDescriptor(), GetOwnerTag(this));
#else
    close(mapped_zip.GetFileDescriptor());
#endif
  }
}

struct CentralDirectoryInfo {
  uint64_t num_records;
  // The size of the central directory (in bytes).
  uint64_t cd_size;
  // The offset of the start of the central directory, relative
  // to the start of the file.
  uint64_t cd_start_offset;
};

// Reads |T| at |readPtr| and increments |readPtr|. Returns std::nullopt if the boundary check
// fails.
template <typename T>
static std::optional<T> TryConsumeUnaligned(uint8_t** readPtr, const uint8_t* bufStart,
                                            size_t bufSize) {
  if (bufSize < sizeof(T) || *readPtr - bufStart > bufSize - sizeof(T)) {
    ALOGW("Zip: %zu byte read exceeds the boundary of allocated buf, offset %zu, bufSize %zu",
          sizeof(T), *readPtr - bufStart, bufSize);
    return std::nullopt;
  }
  return ConsumeUnaligned<T>(readPtr);
}

static ZipError FindCentralDirectoryInfoForZip64(const char* debugFileName, ZipArchive* archive,
                                                 off64_t eocdOffset, CentralDirectoryInfo* cdInfo) {
  if (eocdOffset <= sizeof(Zip64EocdLocator)) {
    ALOGW("Zip: %s: Not enough space for zip64 eocd locator", debugFileName);
    return kInvalidFile;
  }
  // We expect to find the zip64 eocd locator immediately before the zip eocd.
  const int64_t locatorOffset = eocdOffset - sizeof(Zip64EocdLocator);
  Zip64EocdLocator zip64EocdLocatorBuf;
  const auto zip64EocdLocator = reinterpret_cast<const Zip64EocdLocator*>(
      archive->mapped_zip.ReadAtOffset(reinterpret_cast<uint8_t*>((&zip64EocdLocatorBuf)),
                                       sizeof(zip64EocdLocatorBuf), locatorOffset));
  if (!zip64EocdLocator) {
    ALOGW("Zip: %s: Read %zu from offset %" PRId64 " failed %s", debugFileName,
          sizeof(zip64EocdLocatorBuf), locatorOffset, debugFileName);
    return kIoError;
  }

  if (zip64EocdLocator->locator_signature != Zip64EocdLocator::kSignature) {
    ALOGW("Zip: %s: Zip64 eocd locator signature not found at offset %" PRId64, debugFileName,
          locatorOffset);
    return kInvalidFile;
  }

  const int64_t zip64EocdOffset = zip64EocdLocator->zip64_eocd_offset;
  if (locatorOffset <= sizeof(Zip64EocdRecord) ||
      zip64EocdOffset > locatorOffset - sizeof(Zip64EocdRecord)) {
    ALOGW("Zip: %s: Bad zip64 eocd offset %" PRId64 ", eocd locator offset %" PRId64, debugFileName,
          zip64EocdOffset, locatorOffset);
    return kInvalidOffset;
  }

  Zip64EocdRecord zip64EocdRecordBuf;
  const auto zip64EocdRecord = reinterpret_cast<const Zip64EocdRecord*>(
      archive->mapped_zip.ReadAtOffset(reinterpret_cast<uint8_t*>(&zip64EocdRecordBuf),
                                       sizeof(zip64EocdRecordBuf), zip64EocdOffset));
  if (!zip64EocdRecord) {
    ALOGW("Zip: %s: read %zu from offset %" PRId64 " failed %s", debugFileName,
          sizeof(zip64EocdRecordBuf), zip64EocdOffset, debugFileName);
    return kIoError;
  }

  if (zip64EocdRecord->record_signature != Zip64EocdRecord::kSignature) {
    ALOGW("Zip: %s: Zip64 eocd record signature not found at offset %" PRId64, debugFileName,
          zip64EocdOffset);
    return kInvalidFile;
  }

  if (zip64EocdOffset <= zip64EocdRecord->cd_size ||
      zip64EocdRecord->cd_start_offset > zip64EocdOffset - zip64EocdRecord->cd_size) {
    ALOGW("Zip: %s: Bad offset for zip64 central directory. cd offset %" PRIu64 ", cd size %" PRIu64
          ", zip64 eocd offset %" PRIu64,
          debugFileName, zip64EocdRecord->cd_start_offset, zip64EocdRecord->cd_size,
          zip64EocdOffset);
    return kInvalidOffset;
  }

  *cdInfo = {.num_records = zip64EocdRecord->num_records,
             .cd_size = zip64EocdRecord->cd_size,
             .cd_start_offset = zip64EocdRecord->cd_start_offset};

  return kSuccess;
}

static ZipError FindCentralDirectoryInfo(const char* debug_file_name,
                                         ZipArchive* archive,
                                         off64_t file_length,
                                         std::span<uint8_t> scan_buffer,
                                         CentralDirectoryInfo* cdInfo) {
  const auto read_amount = static_cast<uint32_t>(scan_buffer.size());
  const off64_t search_start = file_length - read_amount;

  const auto data = archive->mapped_zip.ReadAtOffset(scan_buffer.data(), read_amount, search_start);
  if (!data) {
    ALOGE("Zip: read %" PRId64 " from offset %" PRId64 " failed", static_cast<int64_t>(read_amount),
          static_cast<int64_t>(search_start));
    return kIoError;
  }

  /*
   * Scan backward for the EOCD magic.  In an archive without a trailing
   * comment, we'll find it on the first try.  (We may want to consider
   * doing an initial minimal read; if we don't find it, retry with a
   * second read as above.)
   */
  CHECK_LE(read_amount, std::numeric_limits<int32_t>::max());
  int32_t i = read_amount - sizeof(EocdRecord);
  for (; i >= 0; i--) {
    if (data[i] == 0x50) {
      const uint32_t* sig_addr = reinterpret_cast<const uint32_t*>(&data[i]);
      if (android::base::get_unaligned<uint32_t>(sig_addr) == EocdRecord::kSignature) {
        ALOGV("+++ Found EOCD at buf+%d", i);
        break;
      }
    }
  }
  if (i < 0) {
    ALOGD("Zip: EOCD not found, %s is not zip", debug_file_name);
    return kInvalidFile;
  }

  const off64_t eocd_offset = search_start + i;
  auto eocd = reinterpret_cast<const EocdRecord*>(data + i);
  /*
   * Verify that there's no trailing space at the end of the central directory
   * and its comment.
   */
  const off64_t calculated_length = eocd_offset + sizeof(EocdRecord) + eocd->comment_length;
  if (calculated_length != file_length) {
    ALOGW("Zip: %" PRId64 " extraneous bytes at the end of the central directory",
          static_cast<int64_t>(file_length - calculated_length));
    return kInvalidFile;
  }

  // One of the field is 0xFFFFFFFF, look for the zip64 EOCD instead.
  if (eocd->num_records_on_disk == UINT16_MAX || eocd->num_records == UINT16_MAX ||
      eocd->cd_size == UINT32_MAX || eocd->cd_start_offset == UINT32_MAX ||
      eocd->comment_length == UINT16_MAX) {
    ALOGV("Looking for the zip64 EOCD (cd_size: %" PRIu32 ", cd_start_offset: %" PRIu32
          ", comment_length: %" PRIu16 ", num_records: %" PRIu16 ", num_records_on_disk: %" PRIu16
          ")",
          eocd->cd_size, eocd->cd_start_offset, eocd->comment_length, eocd->num_records,
          eocd->num_records_on_disk);
    return FindCentralDirectoryInfoForZip64(debug_file_name, archive, eocd_offset, cdInfo);
  }

  /*
   * Grab the CD offset and size, and the number of entries in the
   * archive and verify that they look reasonable.
   */
  if (static_cast<off64_t>(eocd->cd_start_offset) + eocd->cd_size > eocd_offset) {
    ALOGW("Zip: bad offsets (dir %" PRIu32 ", size %" PRIu32 ", eocd %" PRId64 ")",
          eocd->cd_start_offset, eocd->cd_size, static_cast<int64_t>(eocd_offset));
    return kInvalidOffset;
  }

  *cdInfo = {.num_records = eocd->num_records,
             .cd_size = eocd->cd_size,
             .cd_start_offset = eocd->cd_start_offset};
  return kSuccess;
}

/*
 * Find the zip Central Directory and memory-map it.
 *
 * On success, returns kSuccess after populating fields from the EOCD area:
 *   directory_offset
 *   directory_ptr
 *   num_entries
 */
static ZipError MapCentralDirectory(const char* debug_file_name, ZipArchive* archive) {
  // Test file length. We want to make sure the file is small enough to be a zip
  // file.
  off64_t file_length = archive->mapped_zip.GetFileLength();
  if (file_length == -1) {
    return kInvalidFile;
  }

  if (file_length > kMaxFileLength) {
    ALOGV("Zip: zip file too long %" PRId64, static_cast<int64_t>(file_length));
    return kInvalidFile;
  }

  if (file_length < static_cast<off64_t>(sizeof(EocdRecord))) {
    ALOGV("Zip: length %" PRId64 " is too small to be zip", static_cast<int64_t>(file_length));
    return kInvalidFile;
  }

  /*
   * Perform the traditional EOCD snipe hunt.
   *
   * We're searching for the End of Central Directory magic number,
   * which appears at the start of the EOCD block.  It's followed by
   * 18 bytes of EOCD stuff and up to 64KB of archive comment.  We
   * need to read the last part of the file into a buffer, dig through
   * it to find the magic number, parse some values out, and use those
   * to determine the extent of the CD.
   *
   * We start by pulling in the last part of the file.
   */
  const auto read_amount = uint32_t(std::min<off64_t>(file_length, kMaxEOCDSearch));

  CentralDirectoryInfo cdInfo = {};
  std::vector<uint8_t> scan_buffer(read_amount);

  SCOPED_SIGBUS_HANDLER({
    incfs::util::clearAndFree(scan_buffer);
    return kIoError;
  });

  if (auto result = FindCentralDirectoryInfo(debug_file_name, archive,
                                             file_length, scan_buffer, &cdInfo);
      result != kSuccess) {
    return result;
  }

  scan_buffer.clear();

  if (cdInfo.num_records == 0) {
#if defined(__ANDROID__)
    ALOGW("Zip: empty archive?");
#endif
    return kEmptyArchive;
  }

  if (cdInfo.cd_size >= SIZE_MAX) {
    ALOGW("Zip: The size of central directory doesn't fit in range of size_t: %" PRIu64,
          cdInfo.cd_size);
    return kInvalidFile;
  }

  ALOGV("+++ num_entries=%" PRIu64 " dir_size=%" PRIu64 " dir_offset=%" PRIu64, cdInfo.num_records,
        cdInfo.cd_size, cdInfo.cd_start_offset);

  // It all looks good.  Create a mapping for the CD, and set the fields in archive.
  if (!archive->InitializeCentralDirectory(static_cast<off64_t>(cdInfo.cd_start_offset),
                                           static_cast<size_t>(cdInfo.cd_size))) {
    return kMmapFailed;
  }

  archive->num_entries = cdInfo.num_records;
  archive->directory_offset = cdInfo.cd_start_offset;

  return kSuccess;
}

static ZipError ParseZip64ExtendedInfoInExtraField(
    const uint8_t* extraFieldStart, uint16_t extraFieldLength, uint32_t zip32UncompressedSize,
    uint32_t zip32CompressedSize, std::optional<uint32_t> zip32LocalFileHeaderOffset,
    Zip64ExtendedInfo* zip64Info) {
  if (extraFieldLength <= 4) {
    ALOGW("Zip: Extra field isn't large enough to hold zip64 info, size %" PRIu16,
          extraFieldLength);
    return kInvalidFile;
  }

  // Each header MUST consist of:
  // Header ID - 2 bytes
  // Data Size - 2 bytes
  uint16_t offset = 0;
  while (offset < extraFieldLength - 4) {
    auto readPtr = const_cast<uint8_t*>(extraFieldStart + offset);
    auto headerId = ConsumeUnaligned<uint16_t>(&readPtr);
    auto dataSize = ConsumeUnaligned<uint16_t>(&readPtr);

    offset += 4;
    if (dataSize > extraFieldLength - offset) {
      ALOGW("Zip: Data size exceeds the boundary of extra field, data size %" PRIu16, dataSize);
      return kInvalidOffset;
    }

    // Skip the other types of extensible data fields. Details in
    // https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT section 4.5
    if (headerId != Zip64ExtendedInfo::kHeaderId) {
      offset += dataSize;
      continue;
    }
    // Layout for Zip64 extended info (not include first 4 bytes of header)
    // Original
    // Size       8 bytes    Original uncompressed file size

    // Compressed
    // Size       8 bytes    Size of compressed data

    // Relative Header
    // Offset     8 bytes    Offset of local header record

    // Disk Start
    // Number     4 bytes    Number of the disk on which
    //                       this file starts
    if (dataSize == 8 * 3 + 4) {
      ALOGW(
          "Zip: Found `Disk Start Number` field in extra block. Ignoring it.");
      dataSize -= 4;
    }
    // Sometimes, only a subset of {uncompressed size, compressed size, relative
    // header offset} is presents. but golang's zip writer will write out all
    // 3 even if only 1 is necessary. We should parse all 3 fields if they are
    // there.
    const bool completeField = dataSize == 8 * 3;

    std::optional<uint64_t> uncompressedFileSize;
    std::optional<uint64_t> compressedFileSize;
    std::optional<uint64_t> localHeaderOffset;
    if (zip32UncompressedSize == UINT32_MAX || completeField) {
      uncompressedFileSize = TryConsumeUnaligned<uint64_t>(
          &readPtr, extraFieldStart, extraFieldLength);
      if (!uncompressedFileSize.has_value()) return kInvalidOffset;
    }
    if (zip32CompressedSize == UINT32_MAX || completeField) {
      compressedFileSize = TryConsumeUnaligned<uint64_t>(
          &readPtr, extraFieldStart, extraFieldLength);
      if (!compressedFileSize.has_value()) return kInvalidOffset;
    }
    if (zip32LocalFileHeaderOffset == UINT32_MAX || completeField) {
      localHeaderOffset = TryConsumeUnaligned<uint64_t>(
          &readPtr, extraFieldStart, extraFieldLength);
      if (!localHeaderOffset.has_value()) return kInvalidOffset;
    }

    // calculate how many bytes we read after the data size field.
    size_t bytesRead = readPtr - (extraFieldStart + offset);
    if (bytesRead == 0) {
      ALOGW("Zip: Data size should not be 0 in zip64 extended field");
      return kInvalidFile;
    }

    if (dataSize != bytesRead) {
      auto localOffsetString = zip32LocalFileHeaderOffset.has_value()
                                   ? std::to_string(zip32LocalFileHeaderOffset.value())
                                   : "missing";
      ALOGW("Zip: Invalid data size in zip64 extended field, expect %zu , get %" PRIu16
            ", uncompressed size %" PRIu32 ", compressed size %" PRIu32 ", local header offset %s",
            bytesRead, dataSize, zip32UncompressedSize, zip32CompressedSize,
            localOffsetString.c_str());
      return kInvalidFile;
    }

    zip64Info->uncompressed_file_size = uncompressedFileSize;
    zip64Info->compressed_file_size = compressedFileSize;
    zip64Info->local_header_offset = localHeaderOffset;
    return kSuccess;
  }

  ALOGW("Zip: zip64 extended info isn't found in the extra field.");
  return kInvalidFile;
}

/*
 * Parses the Zip archive's Central Directory.  Allocates and populates the
 * hash table.
 *
 * Returns 0 on success.
 */
static ZipError ParseZipArchive(ZipArchive* archive) {
  SCOPED_SIGBUS_HANDLER(return kIoError);

  maybePrefetch(archive->central_directory.GetBasePtr(), archive->central_directory.GetMapLength());
  const uint8_t* const cd_ptr = archive->central_directory.GetBasePtr();
  const size_t cd_length = archive->central_directory.GetMapLength();
  const uint8_t* const cd_end = cd_ptr + cd_length;
  const uint64_t num_entries = archive->num_entries;
  const uint8_t* ptr = cd_ptr;
  uint16_t max_file_name_length = 0;

  /* Walk through the central directory and verify values */
  for (uint64_t i = 0; i < num_entries; i++) {
    if (ptr > cd_end - sizeof(CentralDirectoryRecord)) {
      ALOGW("Zip: ran off the end (item #%" PRIu64 ", %zu bytes of central directory)", i,
            cd_length);
#if defined(__ANDROID__)
      android_errorWriteLog(0x534e4554, "36392138");
#endif
      return kInvalidFile;
    }

    auto cdr = reinterpret_cast<const CentralDirectoryRecord*>(ptr);
    if (cdr->record_signature != CentralDirectoryRecord::kSignature) {
      ALOGW("Zip: missed a central dir sig (at %" PRIu64 ")", i);
      return kInvalidFile;
    }

    const uint16_t file_name_length = cdr->file_name_length;
    const uint16_t extra_length = cdr->extra_field_length;
    const uint16_t comment_length = cdr->comment_length;
    const uint8_t* file_name = ptr + sizeof(CentralDirectoryRecord);

    if (file_name_length >= cd_length || file_name > cd_end - file_name_length) {
      ALOGW("Zip: file name for entry %" PRIu64
            " exceeds the central directory range, file_name_length: %" PRIu16 ", cd_length: %zu",
            i, file_name_length, cd_length);
      return kInvalidEntryName;
    }

    max_file_name_length = std::max(max_file_name_length, file_name_length);

    const uint8_t* extra_field = file_name + file_name_length;
    if (extra_length >= cd_length || extra_field > cd_end - extra_length) {
      ALOGW("Zip: extra field for entry %" PRIu64
            " exceeds the central directory range, file_name_length: %" PRIu16 ", cd_length: %zu",
            i, extra_length, cd_length);
      return kInvalidFile;
    }

    off64_t local_header_offset = cdr->local_file_header_offset;
    if (local_header_offset == UINT32_MAX) {
      Zip64ExtendedInfo zip64_info{};
      if (auto status = ParseZip64ExtendedInfoInExtraField(
              extra_field, extra_length, cdr->uncompressed_size, cdr->compressed_size,
              cdr->local_file_header_offset, &zip64_info);
          status != kSuccess) {
        return status;
      }
      CHECK(zip64_info.local_header_offset.has_value());
      local_header_offset = zip64_info.local_header_offset.value();
    }

    if (local_header_offset >= archive->directory_offset) {
      ALOGW("Zip: bad LFH offset %" PRId64 " at entry %" PRIu64,
            static_cast<int64_t>(local_header_offset), i);
      return kInvalidFile;
    }

    // Check that file name is valid UTF-8 and doesn't contain NUL (U+0000) characters.
    if (!IsValidEntryName(file_name, file_name_length)) {
      ALOGW("Zip: invalid file name at entry %" PRIu64, i);
      return kInvalidEntryName;
    }

    ptr += sizeof(CentralDirectoryRecord) + file_name_length + extra_length + comment_length;
    if ((ptr - cd_ptr) > static_cast<int64_t>(cd_length)) {
      ALOGW("Zip: bad CD advance (%tu vs %zu) at entry %" PRIu64, ptr - cd_ptr, cd_length, i);
      return kInvalidFile;
    }
  }

  /* Create memory efficient entry map */
  archive->cd_entry_map = CdEntryMapInterface::Create(num_entries, cd_length, max_file_name_length);
  if (archive->cd_entry_map == nullptr) {
    return kAllocationFailed;
  }

  /* Central directory verified, now add entries to the hash table */
  ptr = cd_ptr;
  for (uint64_t i = 0; i < num_entries; i++) {
    auto cdr = reinterpret_cast<const CentralDirectoryRecord*>(ptr);
    std::string_view entry_name{reinterpret_cast<const char*>(ptr + sizeof(*cdr)),
                                cdr->file_name_length};
    auto add_result = archive->cd_entry_map->AddToMap(entry_name, cd_ptr);
    if (add_result != 0) {
      ALOGW("Zip: Error adding entry to hash table %d", add_result);
      return add_result;
    }
    ptr += sizeof(*cdr) + cdr->file_name_length + cdr->extra_field_length + cdr->comment_length;
  }

  uint32_t lfh_start_bytes_buf;
  auto lfh_start_bytes = reinterpret_cast<const uint32_t*>(archive->mapped_zip.ReadAtOffset(
      reinterpret_cast<uint8_t*>(&lfh_start_bytes_buf), sizeof(lfh_start_bytes_buf), 0));
  if (!lfh_start_bytes) {
    ALOGW("Zip: Unable to read header for entry at offset == 0.");
    return kInvalidFile;
  }

  if (*lfh_start_bytes != LocalFileHeader::kSignature) {
    ALOGW("Zip: Entry at offset zero has invalid LFH signature %" PRIx32, *lfh_start_bytes);
#if defined(__ANDROID__)
    android_errorWriteLog(0x534e4554, "64211847");
#endif
    return kInvalidFile;
  }

  ALOGV("+++ zip good scan %" PRIu64 " entries", num_entries);

  return kSuccess;
}

static int32_t OpenArchiveInternal(ZipArchive* archive, const char* debug_file_name) {
  int32_t result = MapCentralDirectory(debug_file_name, archive);
  return result != kSuccess ? result : ParseZipArchive(archive);
}

int32_t OpenArchiveFd(int fd, const char* debug_file_name, ZipArchiveHandle* handle,
                      bool assume_ownership) {
  ZipArchive* archive = new ZipArchive(MappedZipFile(fd), assume_ownership);
  *handle = archive;
  return OpenArchiveInternal(archive, debug_file_name);
}

int32_t OpenArchiveFdRange(int fd, const char* debug_file_name, ZipArchiveHandle* handle,
                           off64_t length, off64_t offset, bool assume_ownership) {
  ZipArchive* archive = new ZipArchive(MappedZipFile(fd, length, offset), assume_ownership);
  *handle = archive;

  if (length < 0) {
    ALOGW("Invalid zip length %" PRId64, length);
    return kIoError;
  }

  if (offset < 0) {
    ALOGW("Invalid zip offset %" PRId64, offset);
    return kIoError;
  }

  return OpenArchiveInternal(archive, debug_file_name);
}

int32_t OpenArchive(const char* fileName, ZipArchiveHandle* handle) {
  const int fd = ::android::base::utf8::open(fileName, O_RDONLY | O_BINARY | O_CLOEXEC, 0);
  ZipArchive* archive = new ZipArchive(MappedZipFile(fd), true);
  *handle = archive;

  if (fd < 0) {
    ALOGW("Unable to open '%s': %s", fileName, strerror(errno));
    return kIoError;
  }

  return OpenArchiveInternal(archive, fileName);
}

int32_t OpenArchiveFromMemory(const void* address, size_t length, const char* debug_file_name,
                              ZipArchiveHandle* handle) {
  ZipArchive* archive = new ZipArchive(address, length);
  *handle = archive;
  return OpenArchiveInternal(archive, debug_file_name);
}

ZipArchiveInfo GetArchiveInfo(ZipArchiveHandle archive) {
  ZipArchiveInfo result;
  result.archive_size = archive->mapped_zip.GetFileLength();
  result.entry_count = archive->num_entries;
  return result;
}

/*
 * Close a ZipArchive, closing the file and freeing the contents.
 */
void CloseArchive(ZipArchiveHandle archive) {
  ALOGV("Closing archive %p", archive);
  delete archive;
}

static int32_t ValidateDataDescriptor(MappedZipFile& mapped_zip, const ZipEntry64* entry) {
  SCOPED_SIGBUS_HANDLER(return kIoError);

  // Maximum possible size for data descriptor: 2 * 4 + 2 * 8 = 24 bytes
  // The zip format doesn't specify the size of data descriptor. But we won't read OOB here even
  // if the descriptor isn't present. Because the size cd + eocd in the end of the zipfile is
  // larger than 24 bytes. And if the descriptor contains invalid data, we'll abort due to
  // kInconsistentInformation.
  uint8_t ddBuf[24];
  off64_t offset = entry->offset;
  if (entry->method != kCompressStored) {
    offset += entry->compressed_length;
  } else {
    offset += entry->uncompressed_length;
  }

  const auto ddPtr = mapped_zip.ReadAtOffset(ddBuf, sizeof(ddBuf), offset);
  if (!ddPtr) {
    return kIoError;
  }

  const uint32_t ddSignature = *(reinterpret_cast<const uint32_t*>(ddPtr));
  const uint8_t* ddReadPtr = (ddSignature == DataDescriptor::kOptSignature) ? ddPtr + 4 : ddPtr;
  DataDescriptor descriptor{};
  descriptor.crc32 = ConsumeUnaligned<uint32_t>(&ddReadPtr);
  // Don't use entry->zip64_format_size, because that is set to true even if
  // both compressed/uncompressed size are < 0xFFFFFFFF.
  constexpr auto u32max = std::numeric_limits<uint32_t>::max();
  if (entry->compressed_length >= u32max ||
      entry->uncompressed_length >= u32max) {
    descriptor.compressed_size = ConsumeUnaligned<uint64_t>(&ddReadPtr);
    descriptor.uncompressed_size = ConsumeUnaligned<uint64_t>(&ddReadPtr);
  } else {
    descriptor.compressed_size = ConsumeUnaligned<uint32_t>(&ddReadPtr);
    descriptor.uncompressed_size = ConsumeUnaligned<uint32_t>(&ddReadPtr);
  }

  // Validate that the values in the data descriptor match those in the central
  // directory.
  if (entry->compressed_length != descriptor.compressed_size ||
      entry->uncompressed_length != descriptor.uncompressed_size ||
      entry->crc32 != descriptor.crc32) {
    ALOGW("Zip: size/crc32 mismatch. expected {%" PRIu64 ", %" PRIu64 ", %" PRIx32
          "}, was {%" PRIu64 ", %" PRIu64 ", %" PRIx32 "}",
          entry->compressed_length, entry->uncompressed_length, entry->crc32,
          descriptor.compressed_size, descriptor.uncompressed_size, descriptor.crc32);
    return kInconsistentInformation;
  }

  return 0;
}

static int32_t FindEntry(const ZipArchive* archive, std::string_view entryName,
                         const uint64_t nameOffset, ZipEntry64* data) {
  std::vector<uint8_t> buffer;
  SCOPED_SIGBUS_HANDLER({
    incfs::util::clearAndFree(buffer);
    return kIoError;
  });

  // Recover the start of the central directory entry from the filename
  // pointer.  The filename is the first entry past the fixed-size data,
  // so we can just subtract back from that.
  const uint8_t* base_ptr = archive->central_directory.GetBasePtr();
  const uint8_t* ptr = base_ptr + nameOffset;
  ptr -= sizeof(CentralDirectoryRecord);

  // This is the base of our mmapped region, we have to check that
  // the name that's in the hash table is a pointer to a location within
  // this mapped region.
  if (ptr < base_ptr || ptr > base_ptr + archive->central_directory.GetMapLength()) {
    ALOGW("Zip: Invalid entry pointer");
    return kInvalidOffset;
  }

  auto cdr = reinterpret_cast<const CentralDirectoryRecord*>(ptr);

  // The offset of the start of the central directory in the zipfile.
  // We keep this lying around so that we can check all our lengths
  // and our per-file structures.
  const off64_t cd_offset = archive->directory_offset;

  // Fill out the compression method, modification time, crc32
  // and other interesting attributes from the central directory. These
  // will later be compared against values from the local file header.
  data->method = cdr->compression_method;
  data->mod_time = cdr->last_mod_date << 16 | cdr->last_mod_time;
  data->crc32 = cdr->crc32;
  data->compressed_length = cdr->compressed_size;
  data->uncompressed_length = cdr->uncompressed_size;

  // Figure out the local header offset from the central directory. The
  // actual file data will begin after the local header and the name /
  // extra comments.
  off64_t local_header_offset = cdr->local_file_header_offset;
  // One of the info field is UINT32_MAX, try to parse the real value in the zip64 extended info in
  // the extra field.
  if (cdr->uncompressed_size == UINT32_MAX || cdr->compressed_size == UINT32_MAX ||
      cdr->local_file_header_offset == UINT32_MAX) {
    const uint8_t* extra_field = ptr + sizeof(CentralDirectoryRecord) + cdr->file_name_length;
    Zip64ExtendedInfo zip64_info{};
    if (auto status = ParseZip64ExtendedInfoInExtraField(
            extra_field, cdr->extra_field_length, cdr->uncompressed_size, cdr->compressed_size,
            cdr->local_file_header_offset, &zip64_info);
        status != kSuccess) {
      return status;
    }

    data->uncompressed_length = zip64_info.uncompressed_file_size.value_or(cdr->uncompressed_size);
    data->compressed_length = zip64_info.compressed_file_size.value_or(cdr->compressed_size);
    local_header_offset = zip64_info.local_header_offset.value_or(local_header_offset);
    data->zip64_format_size =
        cdr->uncompressed_size == UINT32_MAX || cdr->compressed_size == UINT32_MAX;
  }

  off64_t local_header_end;
  if (__builtin_add_overflow(local_header_offset, sizeof(LocalFileHeader), &local_header_end) ||
      local_header_end >= cd_offset) {
    // We tested >= because the name that follows can't be zero length.
    ALOGW("Zip: bad local hdr offset in zip");
    return kInvalidOffset;
  }

  uint8_t lfh_buf[sizeof(LocalFileHeader)];
  const auto lfh = reinterpret_cast<const LocalFileHeader*>(
      archive->mapped_zip.ReadAtOffset(lfh_buf, sizeof(lfh_buf), local_header_offset));
  if (!lfh) {
    ALOGW("Zip: failed reading lfh name from offset %" PRId64,
          static_cast<int64_t>(local_header_offset));
    return kIoError;
  }

  if (lfh->lfh_signature != LocalFileHeader::kSignature) {
    ALOGW("Zip: didn't find signature at start of lfh, offset=%" PRId64,
          static_cast<int64_t>(local_header_offset));
    return kInvalidOffset;
  }

  // Check that the local file header name matches the declared name in the central directory.
  CHECK_LE(entryName.size(), UINT16_MAX);
  auto name_length = static_cast<uint16_t>(entryName.size());
  if (lfh->file_name_length != name_length) {
    ALOGW("Zip: lfh name length did not match central directory for %s: %" PRIu16 " %" PRIu16,
          std::string(entryName).c_str(), lfh->file_name_length, name_length);
    return kInconsistentInformation;
  }
  off64_t name_offset;
  if (__builtin_add_overflow(local_header_offset, sizeof(LocalFileHeader), &name_offset)) {
    ALOGW("Zip: lfh name offset invalid");
    return kInvalidOffset;
  }
  off64_t name_end;
  if (__builtin_add_overflow(name_offset, name_length, &name_end) || name_end > cd_offset) {
    // We tested > cd_offset here because the file data that follows can be zero length.
    ALOGW("Zip: lfh name length invalid");
    return kInvalidOffset;
  }

  // An optimization: get enough memory on the stack to be able to use it later without an extra
  // allocation when reading the zip64 extended info. Reasonable names should be under half the
  // MAX_PATH (256 chars), and Zip64 header size is 32 bytes; archives often have some other extras,
  // e.g. alignment, so 128 bytes is outght to be enough for (almost) anybody. If it's not we'll
  // reallocate later anyway.
  uint8_t static_buf[128];
  auto name_buf = static_buf;
  if (name_length > std::size(static_buf)) {
    buffer.resize(name_length);
    name_buf = buffer.data();
  }
  const auto read_name = archive->mapped_zip.ReadAtOffset(name_buf, name_length, name_offset);
  if (!read_name) {
    ALOGW("Zip: failed reading lfh name from offset %" PRId64, static_cast<int64_t>(name_offset));
    return kIoError;
  }
  if (memcmp(entryName.data(), read_name, name_length) != 0) {
    ALOGW("Zip: lfh name did not match central directory");
    return kInconsistentInformation;
  }

  // Check the extra field length, regardless of whether it's used, or what it's used for.
  const off64_t lfh_extra_field_offset = name_offset + lfh->file_name_length;
  const uint16_t lfh_extra_field_size = lfh->extra_field_length;
  if (lfh_extra_field_offset > cd_offset - lfh_extra_field_size) {
    ALOGW("Zip: extra field has a bad size for entry %s", std::string(entryName).c_str());
    return kInvalidOffset;
  }

  data->extra_field_size = lfh_extra_field_size;

  // Check whether the extra field is being used for zip64.
  uint64_t lfh_uncompressed_size = lfh->uncompressed_size;
  uint64_t lfh_compressed_size = lfh->compressed_size;
  if (lfh_uncompressed_size == UINT32_MAX || lfh_compressed_size == UINT32_MAX) {
    if (lfh_uncompressed_size != UINT32_MAX || lfh_compressed_size != UINT32_MAX) {
      ALOGW(
          "Zip: zip64 on Android requires both compressed and uncompressed length to be "
          "UINT32_MAX");
      return kInvalidFile;
    }

    auto lfh_extra_field_buf = static_buf;
    if (lfh_extra_field_size > std::size(static_buf)) {
      // Make sure vector won't try to copy existing data if it needs to reallocate.
      buffer.clear();
      buffer.resize(lfh_extra_field_size);
      lfh_extra_field_buf = buffer.data();
    }
    const auto local_extra_field = archive->mapped_zip.ReadAtOffset(
        lfh_extra_field_buf, lfh_extra_field_size, lfh_extra_field_offset);
    if (!local_extra_field) {
      ALOGW("Zip: failed reading lfh extra field from offset %" PRId64, lfh_extra_field_offset);
      return kIoError;
    }

    Zip64ExtendedInfo zip64_info{};
    if (auto status = ParseZip64ExtendedInfoInExtraField(
            local_extra_field, lfh_extra_field_size, lfh->uncompressed_size, lfh->compressed_size,
            std::nullopt, &zip64_info);
        status != kSuccess) {
      return status;
    }

    CHECK(zip64_info.uncompressed_file_size.has_value());
    CHECK(zip64_info.compressed_file_size.has_value());
    lfh_uncompressed_size = zip64_info.uncompressed_file_size.value();
    lfh_compressed_size = zip64_info.compressed_file_size.value();
  }

  // Paranoia: Match the values specified in the local file header
  // to those specified in the central directory.

  // Warn if central directory and local file header don't agree on the use
  // of a trailing Data Descriptor. The reference implementation is inconsistent
  // and appears to use the LFH value during extraction (unzip) but the CD value
  // while displayng information about archives (zipinfo). The spec remains
  // silent on this inconsistency as well.
  //
  // For now, always use the version from the LFH but make sure that the values
  // specified in the central directory match those in the data descriptor.
  //
  // NOTE: It's also worth noting that unzip *does* warn about inconsistencies in
  // bit 11 (EFS: The language encoding flag, marking that filename and comment are
  // encoded using UTF-8). This implementation does not check for the presence of
  // that flag and always enforces that entry names are valid UTF-8.
  if ((lfh->gpb_flags & kGPBDDFlagMask) != (cdr->gpb_flags & kGPBDDFlagMask)) {
    ALOGW("Zip: gpb flag mismatch at bit 3. expected {%04" PRIx16 "}, was {%04" PRIx16 "}",
          cdr->gpb_flags, lfh->gpb_flags);
  }

  // If there is no trailing data descriptor, verify that the central directory and local file
  // header agree on the crc, compressed, and uncompressed sizes of the entry.
  if ((lfh->gpb_flags & kGPBDDFlagMask) == 0) {
    data->has_data_descriptor = 0;
    if (data->compressed_length != lfh_compressed_size ||
        data->uncompressed_length != lfh_uncompressed_size || data->crc32 != lfh->crc32) {
      ALOGW("Zip: size/crc32 mismatch. expected {%" PRIu64 ", %" PRIu64 ", %" PRIx32
            "}, was {%" PRIu64 ", %" PRIu64 ", %" PRIx32 "}",
            data->compressed_length, data->uncompressed_length, data->crc32, lfh_compressed_size,
            lfh_uncompressed_size, lfh->crc32);
      return kInconsistentInformation;
    }
  } else {
    data->has_data_descriptor = 1;
  }

  // 4.4.2.1: the upper byte of `version_made_by` gives the source OS. Unix is 3.
  data->version_made_by = cdr->version_made_by;
  data->external_file_attributes = cdr->external_file_attributes;
  if ((data->version_made_by >> 8) == 3) {
    data->unix_mode = (cdr->external_file_attributes >> 16) & 0xffff;
  } else {
    data->unix_mode = 0777;
  }

  // 4.4.4: general purpose bit flags.
  data->gpbf = lfh->gpb_flags;

  // 4.4.14: the lowest bit of the internal file attributes field indicates text.
  // Currently only needed to implement zipinfo.
  data->is_text = (cdr->internal_file_attributes & 1);

  const off64_t data_offset = local_header_offset + sizeof(LocalFileHeader) +
                              lfh->file_name_length + lfh->extra_field_length;
  if (data_offset > cd_offset) {
    ALOGW("Zip: bad data offset %" PRId64 " in zip", static_cast<int64_t>(data_offset));
    return kInvalidOffset;
  }

  if (data->compressed_length > cd_offset - data_offset) {
    ALOGW("Zip: bad compressed length in zip (%" PRId64 " + %" PRIu64 " > %" PRId64 ")",
          static_cast<int64_t>(data_offset), data->compressed_length,
          static_cast<int64_t>(cd_offset));
    return kInvalidOffset;
  }

  if (data->method == kCompressStored && data->uncompressed_length > cd_offset - data_offset) {
    ALOGW("Zip: bad uncompressed length in zip (%" PRId64 " + %" PRIu64 " > %" PRId64 ")",
          static_cast<int64_t>(data_offset), data->uncompressed_length,
          static_cast<int64_t>(cd_offset));
    return kInvalidOffset;
  }

  data->offset = data_offset;
  return 0;
}

struct IterationHandle {
  ZipArchive* archive;

  std::function<bool(std::string_view)> matcher;

  uint32_t position = 0;

  IterationHandle(ZipArchive* archive, std::function<bool(std::string_view)> in_matcher)
      : archive(archive), matcher(std::move(in_matcher)) {}

  bool Match(std::string_view entry_name) const { return !matcher || matcher(entry_name); }
};

int32_t StartIteration(ZipArchiveHandle archive, void** cookie_ptr,
                       const std::string_view optional_prefix,
                       const std::string_view optional_suffix) {
  if (optional_prefix.size() > static_cast<size_t>(UINT16_MAX) ||
      optional_suffix.size() > static_cast<size_t>(UINT16_MAX)) {
    ALOGW("Zip: prefix/suffix too long");
    return kInvalidEntryName;
  }
  if (optional_prefix.empty() && optional_suffix.empty()) {
    return StartIteration(archive, cookie_ptr, std::function<bool(std::string_view)>{});
  }
  auto matcher = [prefix = std::string(optional_prefix),
                  suffix = std::string(optional_suffix)](std::string_view name) mutable {
    return android::base::StartsWith(name, prefix) && android::base::EndsWith(name, suffix);
  };
  return StartIteration(archive, cookie_ptr, std::move(matcher));
}

int32_t StartIteration(ZipArchiveHandle archive, void** cookie_ptr,
                       std::function<bool(std::string_view)> matcher) {
  if (archive == nullptr || archive->cd_entry_map == nullptr) {
    ALOGW("Zip: Invalid ZipArchiveHandle");
    return kInvalidHandle;
  }

  archive->cd_entry_map->ResetIteration();
  *cookie_ptr = new IterationHandle(archive, std::move(matcher));
  return 0;
}

void EndIteration(void* cookie) {
  delete reinterpret_cast<IterationHandle*>(cookie);
}

int32_t ZipEntry::CopyFromZipEntry64(ZipEntry* dst, const ZipEntry64* src) {
  if (src->compressed_length > UINT32_MAX || src->uncompressed_length > UINT32_MAX) {
    ALOGW(
        "Zip: the entry size is too large to fit into the 32 bits ZipEntry, uncompressed "
        "length %" PRIu64 ", compressed length %" PRIu64,
        src->uncompressed_length, src->compressed_length);
    return kUnsupportedEntrySize;
  }

  *dst = *src;
  dst->uncompressed_length = static_cast<uint32_t>(src->uncompressed_length);
  dst->compressed_length = static_cast<uint32_t>(src->compressed_length);
  return kSuccess;
}

int32_t FindEntry(const ZipArchiveHandle archive, const std::string_view entryName,
                  ZipEntry* data) {
  ZipEntry64 entry64;
  if (auto status = FindEntry(archive, entryName, &entry64); status != kSuccess) {
    return status;
  }

  return ZipEntry::CopyFromZipEntry64(data, &entry64);
}

int32_t FindEntry(const ZipArchiveHandle archive, const std::string_view entryName,
                  ZipEntry64* data) {
  if (entryName.empty() || entryName.size() > static_cast<size_t>(UINT16_MAX)) {
    ALOGW("Zip: Invalid filename of length %zu", entryName.size());
    return kInvalidEntryName;
  }

  const auto [result, offset] =
      archive->cd_entry_map->GetCdEntryOffset(entryName, archive->central_directory.GetBasePtr());
  if (result != 0) {
    ALOGV("Zip: Could not find entry %.*s", static_cast<int>(entryName.size()), entryName.data());
    return static_cast<int32_t>(result);  // kEntryNotFound is safe to truncate.
  }
  // We know there are at most hash_table_size entries, safe to truncate.
  return FindEntry(archive, entryName, offset, data);
}

int32_t Next(void* cookie, ZipEntry* data, std::string* name) {
  ZipEntry64 entry64;
  if (auto status = Next(cookie, &entry64, name); status != kSuccess) {
    return status;
  }

  return ZipEntry::CopyFromZipEntry64(data, &entry64);
}

int32_t Next(void* cookie, ZipEntry* data, std::string_view* name) {
  ZipEntry64 entry64;
  if (auto status = Next(cookie, &entry64, name); status != kSuccess) {
    return status;
  }

  return ZipEntry::CopyFromZipEntry64(data, &entry64);
}

int32_t Next(void* cookie, ZipEntry64* data, std::string* name) {
  std::string_view sv;
  int32_t result = Next(cookie, data, &sv);
  if (result == 0 && name) {
    *name = std::string(sv);
  }
  return result;
}

int32_t Next(void* cookie, ZipEntry64* data, std::string_view* name) {
  IterationHandle* handle = reinterpret_cast<IterationHandle*>(cookie);
  if (handle == nullptr) {
    ALOGW("Zip: Null ZipArchiveHandle");
    return kInvalidHandle;
  }

  ZipArchive* archive = handle->archive;
  if (archive == nullptr || archive->cd_entry_map == nullptr) {
    ALOGW("Zip: Invalid ZipArchiveHandle");
    return kInvalidHandle;
  }

  SCOPED_SIGBUS_HANDLER(return kIoError);

  auto entry = archive->cd_entry_map->Next(archive->central_directory.GetBasePtr());
  while (entry != std::pair<std::string_view, uint64_t>()) {
    const auto [entry_name, offset] = entry;
    if (handle->Match(entry_name)) {
      const int error = FindEntry(archive, entry_name, offset, data);
      if (!error && name) {
        *name = entry_name;
      }
      return error;
    }
    entry = archive->cd_entry_map->Next(archive->central_directory.GetBasePtr());
  }

  archive->cd_entry_map->ResetIteration();
  return kIterationEnd;
}

// A Writer that writes data to a fixed size memory region.
// The size of the memory region must be equal to the total size of
// the data appended to it.
class MemoryWriter final : public zip_archive::Writer {
 public:
  static std::optional<MemoryWriter> Create(uint8_t* buf, size_t size,
                                            const ZipEntry64* entry) {
    const uint64_t declared_length = entry->uncompressed_length;
    if (declared_length > size) {
      ALOGE("Zip: file size %" PRIu64 " is larger than the buffer size %zu.", declared_length,
            size);
      return {};
    }

    return std::make_optional<MemoryWriter>(buf, size);
  }

  virtual bool Append(uint8_t* buf, size_t buf_size) override {
    if (buf_size == 0 || (buf >= buf_ && buf < buf_ + size_)) {
      return true;
    }

    if (size_ < buf_size || bytes_written_ > size_ - buf_size) {
      ALOGE("Zip: Unexpected size %zu (declared) vs %zu (actual)", size_,
            bytes_written_ + buf_size);
      return false;
    }

    memcpy(buf_ + bytes_written_, buf, buf_size);
    bytes_written_ += buf_size;
    return true;
  }

  Buffer GetBuffer(size_t length) override {
    if (length > size_) {
      // Special case for empty files: zlib wants at least some buffer but won't ever write there.
      if (size_ == 0 && length <= sizeof(bytes_written_)) {
        return {reinterpret_cast<uint8_t*>(&bytes_written_), length};
      }
      return {};
    }
    return {buf_, length};
  }

  MemoryWriter(uint8_t* buf, size_t size) : buf_(buf), size_(size), bytes_written_(0) {}

 private:
  uint8_t* const buf_{nullptr};
  const size_t size_;
  size_t bytes_written_;
};

// A Writer that appends data to a file |fd| at its current position.
// The file will be truncated to the end of the written data.
class FileWriter final : public zip_archive::Writer {
 public:
  // Creates a FileWriter for |fd| and prepare to write |entry| to it,
  // guaranteeing that the file descriptor is valid and that there's enough
  // space on the volume to write out the entry completely and that the file
  // is truncated to the correct length (no truncation if |fd| references a
  // block device).
  //
  // Returns a valid FileWriter on success, |nullopt| if an error occurred.
  static std::optional<FileWriter> Create(int fd, const ZipEntry64* entry) {
    const uint64_t declared_length = entry->uncompressed_length;
    const off64_t current_offset = lseek64(fd, 0, SEEK_CUR);
    if (current_offset == -1) {
      ALOGE("Zip: unable to seek to current location on fd %d: %s", fd, strerror(errno));
      return {};
    }

    // fallocate() takes a signed size, so restrict the length to avoid errors.
    if (declared_length > INT64_MAX) {
      ALOGE("Zip: file size %" PRIu64 " is too large to extract.", declared_length);
      return {};
    }

#if defined(__linux__)
    if (declared_length > 0) {
      // Make sure we have enough space on the volume to extract the compressed
      // entry. Note that the call to ftruncate below will change the file size but
      // will not allocate space on disk and this call to fallocate will not
      // change the file size.
      // Note: fallocate is only supported by the following filesystems -
      // btrfs, ext4, ocfs2, and xfs. Therefore fallocate might fail with
      // EOPNOTSUPP error when issued in other filesystems.
      // Hence, check for the return error code before concluding that the
      // disk does not have enough space.
      long result = TEMP_FAILURE_RETRY(fallocate(fd, 0, current_offset, declared_length));
      if (result == -1 && errno == ENOSPC) {
        ALOGE("Zip: unable to allocate %" PRIu64 " bytes at offset %" PRId64 ": %s",
              declared_length, current_offset, strerror(errno));
        return {};
      }
    }
#endif  // __linux__

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
      ALOGE("Zip: unable to fstat file: %s", strerror(errno));
      return {};
    }

    // Block device doesn't support ftruncate(2).
    if (!S_ISBLK(sb.st_mode)) {
      uint64_t truncate_length;
      if (__builtin_add_overflow(declared_length, current_offset, &truncate_length)) {
        ALOGE("Zip: overflow truncating file (length %" PRId64 ", offset %" PRId64 ")",
              declared_length, current_offset);
        return {};
      }
      long result = TEMP_FAILURE_RETRY(ftruncate(fd, truncate_length));
      if (result == -1) {
        ALOGE("Zip: unable to truncate file to %" PRId64 ": %s", truncate_length, strerror(errno));
        return {};
      }
    }

    return std::make_optional<FileWriter>(fd, declared_length);
  }

  virtual bool Append(uint8_t* buf, size_t buf_size) override {
    if (declared_length_ < buf_size || total_bytes_written_ > declared_length_ - buf_size) {
      ALOGE("Zip: Unexpected size %" PRIu64 "  (declared) vs %" PRIu64 " (actual)",
            declared_length_, total_bytes_written_ + buf_size);
      return false;
    }

    const bool result = android::base::WriteFully(fd_, buf, buf_size);
    if (result) {
      total_bytes_written_ += buf_size;
    } else {
      ALOGE("Zip: unable to write %zu bytes to file; %s", buf_size, strerror(errno));
    }

    return result;
  }

  explicit FileWriter(const int fd = -1, const uint64_t declared_length = 0)
      : Writer(), fd_(fd), declared_length_(declared_length), total_bytes_written_(0) {}

 private:
  int fd_;
  const uint64_t declared_length_;
  uint64_t total_bytes_written_;
};

class EntryReader final : public zip_archive::Reader {
 public:
  EntryReader(const MappedZipFile& zip_file, const ZipEntry64* entry)
      : Reader(), zip_file_(zip_file), entry_(entry) {}

  bool ReadAtOffset(uint8_t* buf, size_t len, off64_t offset) const override {
    const auto res = zip_file_.ReadAtOffset(buf, len, entry_->offset + offset);
    if (!res) return false;
    if (res != buf) {
      memcpy(buf, res, len);
    }
    return true;
  }

  const uint8_t* AccessAtOffset(uint8_t* buf, size_t len, off64_t offset) const override {
    return zip_file_.ReadAtOffset(buf, len, entry_->offset + offset);
  }

  bool IsZeroCopy() const override { return zip_file_.GetBasePtr() != nullptr; }

 private:
  const MappedZipFile& zip_file_;
  const ZipEntry64* entry_;
};

// This method is using libz macros with old-style-casts
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
static inline int zlib_inflateInit2(z_stream* stream, int window_bits) {
  return inflateInit2(stream, window_bits);
}
#pragma GCC diagnostic pop

namespace zip_archive {

// Moved out of line to avoid -Wweak-vtables.
auto Writer::GetBuffer(size_t) -> Buffer {
  return {};
}

const uint8_t* Reader::AccessAtOffset(uint8_t* buf, size_t len, off64_t offset) const {
  return ReadAtOffset(buf, len, offset) ? buf : nullptr;
}

bool Reader::IsZeroCopy() const {
  return false;
}

}  // namespace zip_archive

static std::span<uint8_t> bufferToSpan(zip_archive::Writer::Buffer buf) {
  return std::span<uint8_t>(buf.first, buf.second);
}

template <bool OnIncfs>
static int32_t inflateImpl(const zip_archive::Reader& reader,
                           const uint64_t compressed_length,
                           const uint64_t uncompressed_length,
                           zip_archive::Writer* writer, uint64_t* crc_out) {
  constexpr uint64_t kBufSize = 32768;

  std::vector<uint8_t> read_buf;
  uint64_t max_read_size;
  if (reader.IsZeroCopy()) {
    max_read_size = std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), compressed_length);
  } else {
    max_read_size = std::min(compressed_length, kBufSize);
    read_buf.resize(static_cast<size_t>(max_read_size));
  }

  std::vector<uint8_t> write_buf;
  // For some files zlib needs more space than the uncompressed buffer size, e.g. when inflating
  // an empty file.
  const auto min_write_buffer_size = std::max(compressed_length, uncompressed_length);
  auto write_span = bufferToSpan(writer->GetBuffer(size_t(min_write_buffer_size)));
  bool direct_writer;
  if (write_span.size() >= min_write_buffer_size) {
    direct_writer = true;
  } else {
    direct_writer = false;
    write_buf.resize(static_cast<size_t>(std::min(min_write_buffer_size, kBufSize)));
    write_span = write_buf;
  }

  /*
   * Initialize the zlib stream struct.
   */
  z_stream zstream = {};
  zstream.zalloc = Z_NULL;
  zstream.zfree = Z_NULL;
  zstream.opaque = Z_NULL;
  zstream.next_in = NULL;
  zstream.avail_in = 0;
  zstream.next_out = write_span.data();
  zstream.avail_out = static_cast<uint32_t>(write_span.size());
  zstream.data_type = Z_UNKNOWN;

  /*
   * Use the undocumented "negative window bits" feature to tell zlib
   * that there's no zlib header waiting for it.
   */
  int zerr = zlib_inflateInit2(&zstream, -MAX_WBITS);
  if (zerr != Z_OK) {
    if (zerr == Z_VERSION_ERROR) {
      ALOGE("Installed zlib is not compatible with linked version (%s)", ZLIB_VERSION);
    } else {
      ALOGW("Call to inflateInit2 failed (zerr=%d)", zerr);
    }

    return kZlibError;
  }

  auto zstream_deleter = [](z_stream* stream) {
    inflateEnd(stream); /* free up any allocated structures */
  };

  std::unique_ptr<z_stream, decltype(zstream_deleter)> zstream_guard(&zstream, zstream_deleter);
  static_assert(sizeof(zstream_guard) == sizeof(void*));

  SCOPED_SIGBUS_HANDLER_CONDITIONAL(OnIncfs, {
    zstream_guard.reset();
    incfs::util::clearAndFree(read_buf);
    incfs::util::clearAndFree(write_buf);
    return kIoError;
  });

  const bool compute_crc = (crc_out != nullptr);
  uLong crc = 0;
  uint64_t remaining_bytes = compressed_length;
  uint64_t total_output = 0;
  do {
    /* read as much as we can */
    if (zstream.avail_in == 0) {
      const auto read_size = static_cast<uint32_t>(std::min(remaining_bytes, max_read_size));
      const off64_t offset = (compressed_length - remaining_bytes);
      auto buf = reader.AccessAtOffset(read_buf.data(), read_size, offset);
      if (!buf) {
        ALOGW("Zip: inflate read failed, getSize = %u: %s", read_size, strerror(errno));
        return kIoError;
      }

      remaining_bytes -= read_size;

      zstream.next_in = buf;
      zstream.avail_in = read_size;
    }

    /* uncompress the data */
    zerr = inflate(&zstream, Z_NO_FLUSH);
    if (zerr != Z_OK && zerr != Z_STREAM_END) {
      ALOGW("Zip: inflate zerr=%d (nIn=%p aIn=%u nOut=%p aOut=%u)", zerr, zstream.next_in,
            zstream.avail_in, zstream.next_out, zstream.avail_out);
      return kZlibError;
    }

    /* write when we're full or when we're done */
    if (zstream.avail_out == 0 ||
        (zerr == Z_STREAM_END && zstream.avail_out != write_span.size())) {
      const size_t write_size = zstream.next_out - write_span.data();
      if (compute_crc) {
        DCHECK_LE(write_size, write_span.size());
        crc = crc32(crc, write_span.data(), static_cast<uint32_t>(write_size));
      }
      total_output += write_span.size() - zstream.avail_out;

      if (direct_writer) {
        write_span = write_span.subspan(write_size);
      } else if (!writer->Append(write_span.data(), write_size)) {
        return kIoError;
      }

      if (zstream.avail_out == 0) {
        zstream.next_out = write_span.data();
        zstream.avail_out = static_cast<uint32_t>(write_span.size());
      }
    }
  } while (zerr == Z_OK);

  CHECK_EQ(zerr, Z_STREAM_END); /* other errors should've been caught */

  // NOTE: zstream.adler is always set to 0, because we're using the -MAX_WBITS
  // "feature" of zlib to tell it there won't be a zlib file header. zlib
  // doesn't bother calculating the checksum in that scenario. We just do
  // it ourselves above because there are no additional gains to be made by
  // having zlib calculate it for us, since they do it by calling crc32 in
  // the same manner that we have above.
  if (compute_crc) {
    *crc_out = crc;
  }
  if (total_output != uncompressed_length || remaining_bytes != 0) {
    ALOGW("Zip: size mismatch on inflated file (%lu vs %" PRIu64 ")", zstream.total_out,
          uncompressed_length);
    return kInconsistentInformation;
  }

  return 0;
}

static int32_t InflateEntryToWriter(MappedZipFile& mapped_zip, const ZipEntry64* entry,
                                    zip_archive::Writer* writer, uint64_t* crc_out) {
  const EntryReader reader(mapped_zip, entry);
  return inflateImpl<true>(reader, entry->compressed_length,
                           entry->uncompressed_length, writer, crc_out);
}

static int32_t CopyEntryToWriter(MappedZipFile& mapped_zip, const ZipEntry64* entry,
                                 zip_archive::Writer* writer, uint64_t* crc_out) {
  constexpr uint64_t kBufSize = 32768;
  std::vector<uint8_t> buf;
  std::span<uint8_t> write_span{};
  uint64_t max_read_size;
  if (mapped_zip.GetBasePtr() == nullptr ||
      mapped_zip.GetFileLength() < entry->uncompressed_length) {
    // Check if we can read directly into the writer.
    write_span = bufferToSpan(writer->GetBuffer(size_t(entry->uncompressed_length)));
    if (write_span.size() >= entry->uncompressed_length) {
      max_read_size = entry->uncompressed_length;
    } else {
      max_read_size = std::min(entry->uncompressed_length, kBufSize);
      buf.resize((static_cast<size_t>(max_read_size)));
      write_span = buf;
    }
  } else {
    max_read_size = entry->uncompressed_length;
  }

  SCOPED_SIGBUS_HANDLER({
    incfs::util::clearAndFree(buf);
    return kIoError;
  });

  const uint64_t length = entry->uncompressed_length;
  uint64_t count = 0;
  uLong crc = 0;
  while (count < length) {
    uint64_t remaining = length - count;
    off64_t offset = entry->offset + count;

    // Safe conversion because even kBufSize is narrow enough for a 32 bit signed value.
    const auto block_size = static_cast<uint32_t>(std::min(remaining, max_read_size));

    const auto read_buf = mapped_zip.ReadAtOffset(write_span.data(), block_size, offset);
    if (!read_buf) {
      ALOGW("CopyFileToFile: copy read failed, block_size = %u, offset = %" PRId64 ": %s",
            block_size, static_cast<int64_t>(offset), strerror(errno));
      return kIoError;
    }

    if (!writer->Append(const_cast<uint8_t*>(read_buf), block_size)) {
      return kIoError;
    }
    // Advance our span if it's a direct buffer (there's a span but local buffer's empty).
    if (!write_span.empty() && buf.empty()) {
      write_span = write_span.subspan(block_size);
    }
    if (crc_out) {
      crc = crc32(crc, read_buf, block_size);
    }
    count += block_size;
  }

  if (crc_out) {
    *crc_out = crc;
  }

  return 0;
}

static int32_t extractToWriter(ZipArchiveHandle handle, const ZipEntry64* entry,
                               zip_archive::Writer* writer) {
  const uint16_t method = entry->method;

  // this should default to kUnknownCompressionMethod.
  int32_t return_value = -1;
  uint64_t crc = 0;
  if (method == kCompressStored) {
    return_value =
        CopyEntryToWriter(handle->mapped_zip, entry, writer, kCrcChecksEnabled ? &crc : nullptr);
  } else if (method == kCompressDeflated) {
    return_value =
        InflateEntryToWriter(handle->mapped_zip, entry, writer, kCrcChecksEnabled ? &crc : nullptr);
  }

  if (!return_value && entry->has_data_descriptor) {
    return_value = ValidateDataDescriptor(handle->mapped_zip, entry);
    if (return_value) {
      return return_value;
    }
  }

  // Validate that the CRC matches the calculated value.
  if (kCrcChecksEnabled && (entry->crc32 != static_cast<uint32_t>(crc))) {
    ALOGW("Zip: crc mismatch: expected %" PRIu32 ", was %" PRIu64, entry->crc32, crc);
    return kInconsistentInformation;
  }

  return return_value;
}

int32_t ExtractToMemory(ZipArchiveHandle archive, const ZipEntry* entry, uint8_t* begin,
                        size_t size) {
  ZipEntry64 entry64(*entry);
  return ExtractToMemory(archive, &entry64, begin, size);
}

int32_t ExtractToMemory(ZipArchiveHandle archive, const ZipEntry64* entry, uint8_t* begin,
                        size_t size) {
  auto writer = MemoryWriter::Create(begin, size, entry);
  if (!writer) {
    return kIoError;
  }
  return extractToWriter(archive, entry, &writer.value());
}

int32_t ExtractEntryToFile(ZipArchiveHandle archive, const ZipEntry* entry, int fd) {
  ZipEntry64 entry64(*entry);
  return ExtractEntryToFile(archive, &entry64, fd);
}

int32_t ExtractEntryToFile(ZipArchiveHandle archive, const ZipEntry64* entry, int fd) {
  auto writer = FileWriter::Create(fd, entry);
  if (!writer) {
    return kIoError;
  }
  return extractToWriter(archive, entry, &writer.value());
}

int GetFileDescriptor(const ZipArchiveHandle archive) {
  return archive->mapped_zip.GetFileDescriptor();
}

off64_t GetFileDescriptorOffset(const ZipArchiveHandle archive) {
  return archive->mapped_zip.GetFileOffset();
}

//
// ZIPARCHIVE_DISABLE_CALLBACK_API disables all APIs that accept user callbacks.
// It gets defined for the incfs-supporting version of libziparchive, where one
// has to control all the code accessing the archive. See more at
// incfs_support/signal_handling.h
//
#if !ZIPARCHIVE_DISABLE_CALLBACK_API && !defined(_WIN32)
class ProcessWriter final : public zip_archive::Writer {
 public:
  ProcessWriter(ProcessZipEntryFunction func, void* cookie)
      : Writer(), proc_function_(func), cookie_(cookie) {}

  virtual bool Append(uint8_t* buf, size_t buf_size) override {
    return proc_function_(buf, buf_size, cookie_);
  }

 private:
  ProcessZipEntryFunction proc_function_;
  void* cookie_;
};

int32_t ProcessZipEntryContents(ZipArchiveHandle archive, const ZipEntry* entry,
                                ProcessZipEntryFunction func, void* cookie) {
  ZipEntry64 entry64(*entry);
  return ProcessZipEntryContents(archive, &entry64, func, cookie);
}

int32_t ProcessZipEntryContents(ZipArchiveHandle archive, const ZipEntry64* entry,
                                ProcessZipEntryFunction func, void* cookie) {
  ProcessWriter writer(func, cookie);
  return extractToWriter(archive, entry, &writer);
}

#endif  // !ZIPARCHIVE_DISABLE_CALLBACK_API && !defined(_WIN32)

MappedZipFile::MappedZipFile(int fd, off64_t length, off64_t offset)
    : fd_(fd), fd_offset_(offset), data_length_(length) {
  // TODO(b/287285733): restore mmap() when the cold cache regression is fixed.
#if 0
  // Only try to mmap all files in 64-bit+ processes as it's too easy to use up the whole
  // virtual address space on 32-bits, causing out of memory errors later.
  if constexpr (sizeof(void*) >= 8) {
    // Note: GetFileLength() here fills |data_length_| if it was empty.
    // TODO(b/261875471): remove the incfs exclusion when the driver deadlock is fixed.
    if (fd >= 0 && !incfs::util::isIncfsFd(fd) && GetFileLength() > 0 &&
        GetFileLength() < std::numeric_limits<size_t>::max()) {
      mapped_file_ =
          android::base::MappedFile::FromFd(fd, fd_offset_, size_t(data_length_), PROT_READ);
      if (mapped_file_) {
        maybePrepareSequentialReading(mapped_file_->data(), size_t(data_length_));
        base_ptr_ = mapped_file_->data();
      }
    }
  }
#endif  // 0
}

int MappedZipFile::GetFileDescriptor() const {
  return fd_;
}

const void* MappedZipFile::GetBasePtr() const {
  return base_ptr_;
}

off64_t MappedZipFile::GetFileOffset() const {
  return fd_offset_;
}

off64_t MappedZipFile::GetFileLength() const {
  if (data_length_ >= 0) {
    return data_length_;
  }
  if (fd_ < 0) {
    ALOGE("Zip: invalid file map");
  } else {
    struct stat st;
    if (fstat(fd_, &st)) {
      ALOGE("Zip: fstat(%d) failed: %s", fd_, strerror(errno));
    } else {
      if (S_ISBLK(st.st_mode)) {
#if defined(__linux__)
        // Block devices are special - they report 0 as st_size.
        uint64_t size;
        if (ioctl(fd_, BLKGETSIZE64, &size)) {
          ALOGE("Zip: ioctl(%d, BLKGETSIZE64) failed: %s", fd_, strerror(errno));
        } else {
          data_length_ = size - fd_offset_;
        }
#endif
      } else {
        data_length_ = st.st_size - fd_offset_;
      }
    }
  }
  return data_length_;
}

// Attempts to read |len| bytes into |buf| at offset |off|.
const uint8_t* MappedZipFile::ReadAtOffset(uint8_t* buf, size_t len, off64_t off) const {
  if (base_ptr_) {
    if (off < 0 || data_length_ < len || off > data_length_ - len) {
      ALOGE("Zip: invalid offset: %" PRId64 ", read length: %zu, data length: %" PRId64, off, len,
            data_length_);
      return nullptr;
    }
    maybePrefetch(static_cast<const uint8_t*>(base_ptr_) + off, len);
    return static_cast<const uint8_t*>(base_ptr_) + off;
  }
  if (fd_ < 0) {
    ALOGE("Zip: invalid zip file");
    return nullptr;
  }

  if (off < 0) {
    ALOGE("Zip: invalid offset %" PRId64, off);
    return nullptr;
  }

  off64_t read_offset;
  if (__builtin_add_overflow(fd_offset_, off, &read_offset)) {
    ALOGE("Zip: invalid read offset %" PRId64 " overflows, fd offset %" PRId64, off, fd_offset_);
    return nullptr;
  }

  if (data_length_ != -1) {
    off64_t read_end;
    if (len > std::numeric_limits<off64_t>::max() ||
        __builtin_add_overflow(off, static_cast<off64_t>(len), &read_end)) {
      ALOGE("Zip: invalid read length %" PRId64 " overflows, offset %" PRId64,
            static_cast<off64_t>(len), off);
      return nullptr;
    }

    if (read_end > data_length_) {
      ALOGE("Zip: invalid read length %" PRId64 " exceeds data length %" PRId64 ", offset %" PRId64,
            static_cast<off64_t>(len), data_length_, off);
      return nullptr;
    }
  }

  // Make sure to read at offset to ensure concurrent access to the fd.
  if (!android::base::ReadFullyAtOffset(fd_, buf, len, read_offset)) {
    ALOGE("Zip: failed to read at offset %" PRId64, off);
    return nullptr;
  }
  return buf;
}

void CentralDirectory::Initialize(const void* map_base_ptr, off64_t cd_start_offset,
                                  size_t cd_size) {
  base_ptr_ = static_cast<const uint8_t*>(map_base_ptr) + cd_start_offset;
  length_ = cd_size;
}

bool ZipArchive::InitializeCentralDirectory(off64_t cd_start_offset, size_t cd_size) {
  if (!mapped_zip.GetBasePtr()) {
    directory_map = android::base::MappedFile::FromFd(mapped_zip.GetFileDescriptor(),
                                                      mapped_zip.GetFileOffset() + cd_start_offset,
                                                      cd_size, PROT_READ);
    if (!directory_map) {
      ALOGE("Zip: failed to map central directory (offset %" PRId64 ", size %zu): %s",
            cd_start_offset, cd_size, strerror(errno));
      return false;
    }

    CHECK_EQ(directory_map->size(), cd_size);
    central_directory.Initialize(directory_map->data(), 0 /*offset*/, cd_size);
  } else {
    if (mapped_zip.GetBasePtr() == nullptr) {
      ALOGE(
          "Zip: Failed to map central directory, bad mapped_zip base "
          "pointer");
      return false;
    }
    if (static_cast<off64_t>(cd_start_offset) + static_cast<off64_t>(cd_size) >
        mapped_zip.GetFileLength()) {
      ALOGE(
          "Zip: Failed to map central directory, offset exceeds mapped memory region (start_offset "
          "%" PRId64 ", cd_size %zu, mapped_region_size %" PRId64 ")",
          static_cast<int64_t>(cd_start_offset), cd_size, mapped_zip.GetFileLength());
      return false;
    }

    central_directory.Initialize(mapped_zip.GetBasePtr(), cd_start_offset, cd_size);
  }
  return true;
}

// This function returns the embedded timestamp as is and doesn't perform validation.
tm ZipEntryCommon::GetModificationTime() const {
  tm t = {};

  t.tm_hour = (mod_time >> 11) & 0x1f;
  t.tm_min = (mod_time >> 5) & 0x3f;
  t.tm_sec = (mod_time & 0x1f) << 1;

  t.tm_year = ((mod_time >> 25) & 0x7f) + 80;
  t.tm_mon = ((mod_time >> 21) & 0xf) - 1;
  t.tm_mday = (mod_time >> 16) & 0x1f;

  return t;
}

namespace zip_archive {

int32_t Inflate(const Reader& reader, const uint64_t compressed_length,
                const uint64_t uncompressed_length, Writer* writer,
                uint64_t* crc_out) {
  return inflateImpl<false>(reader, compressed_length, uncompressed_length,
                            writer, crc_out);
}

//
// ZIPARCHIVE_DISABLE_CALLBACK_API disables all APIs that accept user callbacks.
// It gets defined for the incfs-supporting version of libziparchive, where one
// has to control all the code accessing the archive. See more at
// incfs_support/signal_handling.h
//
#if !ZIPARCHIVE_DISABLE_CALLBACK_API

int32_t ExtractToWriter(ZipArchiveHandle handle, const ZipEntry64* entry,
                        zip_archive::Writer* writer) {
  return extractToWriter(handle, entry, writer);
}

#endif  // !ZIPARCHIVE_DISABLE_CALLBACK_API

}  // namespace zip_archive
