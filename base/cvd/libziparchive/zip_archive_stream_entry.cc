/*
 * Copyright (C) 2015 The Android Open Source Project
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

#define LOG_TAG "ZIPARCHIVE"

// Read-only stream access to Zip Archive entries.
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <limits>
#include <memory>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <log/log.h>

#include <ziparchive/zip_archive.h>
#include <ziparchive/zip_archive_stream_entry.h>
#include <zlib.h>

#include "zip_archive_private.h"

static constexpr size_t kBufSize = 65535;

bool ZipArchiveStreamEntry::Init(const ZipEntry& entry) {
  crc32_ = entry.crc32;
  offset_ = entry.offset;
  return true;
}

class ZipArchiveStreamEntryUncompressed : public ZipArchiveStreamEntry {
 public:
  explicit ZipArchiveStreamEntryUncompressed(ZipArchiveHandle handle)
      : ZipArchiveStreamEntry(handle) {}
  virtual ~ZipArchiveStreamEntryUncompressed() {}

  const std::vector<uint8_t>* Read() override;

  bool Verify() override;

 protected:
  bool Init(const ZipEntry& entry) override;

  uint32_t length_ = 0u;

 private:
  std::vector<uint8_t> data_;
  uint32_t computed_crc32_ = 0u;
};

bool ZipArchiveStreamEntryUncompressed::Init(const ZipEntry& entry) {
  if (!ZipArchiveStreamEntry::Init(entry)) {
    return false;
  }

  length_ = entry.uncompressed_length;

  data_.resize(kBufSize);
  computed_crc32_ = 0;

  return true;
}

const std::vector<uint8_t>* ZipArchiveStreamEntryUncompressed::Read() {
  // Simple validity check. The vector should *only* be handled by this code. A caller
  // should not const-cast and modify the capacity. This may invalidate next_out.
  //
  // Note: it would be better to store the results of data() across Read calls.
  CHECK_EQ(data_.capacity(), kBufSize);

  if (length_ == 0) {
    return nullptr;
  }

  size_t bytes = (length_ > data_.size()) ? data_.size() : length_;
  ZipArchive* archive = reinterpret_cast<ZipArchive*>(handle_);
  errno = 0;
  auto res = archive->mapped_zip.ReadAtOffset(data_.data(), bytes, offset_);
  if (!res) {
    if (errno != 0) {
      ALOGE("Error reading from archive fd: %s", strerror(errno));
    } else {
      ALOGE("Short read of zip file, possibly corrupted zip?");
    }
    length_ = 0;
    return nullptr;
  }

  if (res != data_.data()) {
    data_.assign(res, res + bytes);
  } else if (bytes < data_.size()) {
    data_.resize(bytes);
  }
  computed_crc32_ = static_cast<uint32_t>(
      crc32(computed_crc32_, data_.data(), static_cast<uint32_t>(data_.size())));
  length_ -= bytes;
  offset_ += bytes;
  return &data_;
}

bool ZipArchiveStreamEntryUncompressed::Verify() {
  return length_ == 0 && crc32_ == computed_crc32_;
}

class ZipArchiveStreamEntryCompressed : public ZipArchiveStreamEntry {
 public:
  explicit ZipArchiveStreamEntryCompressed(ZipArchiveHandle handle)
      : ZipArchiveStreamEntry(handle) {}
  virtual ~ZipArchiveStreamEntryCompressed();

  const std::vector<uint8_t>* Read() override;

  bool Verify() override;

 protected:
  bool Init(const ZipEntry& entry) override;

 private:
  bool z_stream_init_ = false;
  z_stream z_stream_;
  std::vector<uint8_t> in_;
  std::vector<uint8_t> out_;
  uint32_t uncompressed_length_ = 0u;
  uint32_t compressed_length_ = 0u;
  uint32_t computed_crc32_ = 0u;
};

// This method is using libz macros with old-style-casts
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
static inline int zlib_inflateInit2(z_stream* stream, int window_bits) {
  return inflateInit2(stream, window_bits);
}
#pragma GCC diagnostic pop

bool ZipArchiveStreamEntryCompressed::Init(const ZipEntry& entry) {
  if (!ZipArchiveStreamEntry::Init(entry)) {
    return false;
  }

  // Initialize the zlib stream struct.
  memset(&z_stream_, 0, sizeof(z_stream_));
  z_stream_.zalloc = Z_NULL;
  z_stream_.zfree = Z_NULL;
  z_stream_.opaque = Z_NULL;
  z_stream_.next_in = nullptr;
  z_stream_.avail_in = 0;
  z_stream_.avail_out = 0;
  z_stream_.data_type = Z_UNKNOWN;

  // Use the undocumented "negative window bits" feature to tell zlib
  // that there's no zlib header waiting for it.
  int zerr = zlib_inflateInit2(&z_stream_, -MAX_WBITS);
  if (zerr != Z_OK) {
    if (zerr == Z_VERSION_ERROR) {
      ALOGE("Installed zlib is not compatible with linked version (%s)", ZLIB_VERSION);
    } else {
      ALOGE("Call to inflateInit2 failed (zerr=%d)", zerr);
    }

    return false;
  }

  z_stream_init_ = true;

  uncompressed_length_ = entry.uncompressed_length;
  compressed_length_ = entry.compressed_length;

  out_.resize(kBufSize);
  in_.resize(kBufSize);

  computed_crc32_ = 0;

  return true;
}

ZipArchiveStreamEntryCompressed::~ZipArchiveStreamEntryCompressed() {
  if (z_stream_init_) {
    inflateEnd(&z_stream_);
    z_stream_init_ = false;
  }
}

bool ZipArchiveStreamEntryCompressed::Verify() {
  return z_stream_init_ && uncompressed_length_ == 0 && compressed_length_ == 0 &&
         crc32_ == computed_crc32_;
}

const std::vector<uint8_t>* ZipArchiveStreamEntryCompressed::Read() {
  // Simple validity check. The vector should *only* be handled by this code. A caller
  // should not const-cast and modify the capacity. This may invalidate next_out.
  //
  // Note: it would be better to store the results of data() across Read calls.
  CHECK_EQ(out_.capacity(), kBufSize);

  if (z_stream_.avail_out == 0) {
    z_stream_.next_out = out_.data();
    z_stream_.avail_out = static_cast<uint32_t>(out_.size());
  }

  while (true) {
    if (z_stream_.avail_in == 0) {
      if (compressed_length_ == 0) {
        return nullptr;
      }
      DCHECK_LE(in_.size(), std::numeric_limits<uint32_t>::max());  // Should be buf size = 64k.
      auto bytes = std::min(uint32_t(in_.size()), compressed_length_);
      auto archive = reinterpret_cast<ZipArchive*>(handle_);
      errno = 0;
      auto res = archive->mapped_zip.ReadAtOffset(in_.data(), bytes, offset_);
      if (!res) {
        if (errno != 0) {
          ALOGE("Error reading from archive fd: %s", strerror(errno));
        } else {
          ALOGE("Short read of zip file, possibly corrupted zip?");
        }
        return nullptr;
      }

      compressed_length_ -= bytes;
      offset_ += bytes;
      z_stream_.next_in = res;
      z_stream_.avail_in = bytes;
    }

    int zerr = inflate(&z_stream_, Z_NO_FLUSH);
    if (zerr != Z_OK && zerr != Z_STREAM_END) {
      ALOGE("inflate zerr=%d (nIn=%p aIn=%u nOut=%p aOut=%u)", zerr, z_stream_.next_in,
            z_stream_.avail_in, z_stream_.next_out, z_stream_.avail_out);
      return nullptr;
    }

    if (z_stream_.avail_out == 0) {
      uncompressed_length_ -= out_.size();
      computed_crc32_ = static_cast<uint32_t>(
          crc32(computed_crc32_, out_.data(), static_cast<uint32_t>(out_.size())));
      return &out_;
    }
    if (zerr == Z_STREAM_END) {
      if (z_stream_.avail_out != 0) {
        // Resize the vector down to the actual size of the data.
        out_.resize(out_.size() - z_stream_.avail_out);
        computed_crc32_ = static_cast<uint32_t>(
            crc32(computed_crc32_, out_.data(), static_cast<uint32_t>(out_.size())));
        uncompressed_length_ -= out_.size();
        return &out_;
      }
      return nullptr;
    }
  }
  return nullptr;
}

class ZipArchiveStreamEntryRawCompressed : public ZipArchiveStreamEntryUncompressed {
 public:
  explicit ZipArchiveStreamEntryRawCompressed(ZipArchiveHandle handle)
      : ZipArchiveStreamEntryUncompressed(handle) {}
  virtual ~ZipArchiveStreamEntryRawCompressed() {}

  bool Verify() override;

 protected:
  bool Init(const ZipEntry& entry) override;
};

bool ZipArchiveStreamEntryRawCompressed::Init(const ZipEntry& entry) {
  if (!ZipArchiveStreamEntryUncompressed::Init(entry)) {
    return false;
  }
  length_ = entry.compressed_length;

  return true;
}

bool ZipArchiveStreamEntryRawCompressed::Verify() {
  return length_ == 0;
}

ZipArchiveStreamEntry* ZipArchiveStreamEntry::Create(ZipArchiveHandle handle,
                                                     const ZipEntry& entry) {
  ZipArchiveStreamEntry* stream = nullptr;
  if (entry.method != kCompressStored) {
    stream = new ZipArchiveStreamEntryCompressed(handle);
  } else {
    stream = new ZipArchiveStreamEntryUncompressed(handle);
  }
  if (stream && !stream->Init(entry)) {
    delete stream;
    stream = nullptr;
  }

  return stream;
}

ZipArchiveStreamEntry* ZipArchiveStreamEntry::CreateRaw(ZipArchiveHandle handle,
                                                        const ZipEntry& entry) {
  ZipArchiveStreamEntry* stream = nullptr;
  if (entry.method == kCompressStored) {
    // Not compressed, don't need to do anything special.
    stream = new ZipArchiveStreamEntryUncompressed(handle);
  } else {
    stream = new ZipArchiveStreamEntryRawCompressed(handle);
  }
  if (stream && !stream->Init(entry)) {
    delete stream;
    stream = nullptr;
  }
  return stream;
}
