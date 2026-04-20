//
// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/io/cpio.h"

#include <endian.h>
#include <string.h>

#include <charconv>
#include <string_view>

#include "cuttlefish/common/libs/utils/size_utils.h"
#include "cuttlefish/io/read_exact.h"
#include "cuttlefish/io/read_window_view.h"
#include "cuttlefish/result/expect.h"

// For the CPIO file format specification, see:
// https://github.com/libyal/dtformats/blob/c55446a4369149bb109bf783f29a2838c7328718/documentation/Copy%20in%20and%20out%20(CPIO)%20archive%20format.asciidoc

namespace cuttlefish {

namespace {

constexpr std::string_view kMagicNewc1 = "070701";
constexpr std::string_view kMagicNewc2 = "070702";
constexpr std::string_view kMagicOdc = "070707";
constexpr std::string_view kTrailerName = "TRAILER!!!";
constexpr std::string_view kBinaryLeMagic = "\xC7\x71";
constexpr std::string_view kBinaryBeMagic = "\x71\xC7";

// None of these members are null-terminated.
struct CpioNewcHeader {
  char magic[6];
  char ino[8];
  char mode[8];
  char uid[8];
  char gid[8];
  char nlink[8];
  char mtime[8];
  char filesize[8];
  char maj[8];
  char min[8];
  char rmaj[8];
  char rmin[8];
  char namesize[8];
  char chksum[8];
  char filename[];
};

// None of these members are null-terminated.
struct CpioOdcHeader {
  char magic[6];
  char dev[6];
  char ino[6];
  char mode[6];
  char uid[6];
  char gid[6];
  char nlink[6];
  char rdev[6];
  char mtime[11];
  char namesize[6];
  char filesize[11];
  char filename[];
};

struct __attribute__((packed)) CpioBinHeader {
  uint16_t magic;
  uint16_t dev;
  uint16_t ino;
  uint16_t mode;
  uint16_t uid;
  uint16_t gid;
  uint16_t nlink;
  uint16_t rdev;
  uint32_t mtime;
  uint16_t namesize;
  uint32_t filesize;
  char filename[];
};

Result<uint32_t> ParseHex(const char* buf, size_t size) {
  uint32_t val = 0;
  std::from_chars_result result = std::from_chars(buf, buf + size, val, 16);
  CF_EXPECT(result.ec == std::errc(), "Failed to parse hex value");
  return val;
}

Result<uint32_t> ParseOctal(const char* buf, size_t size) {
  uint32_t val = 0;
  std::from_chars_result result = std::from_chars(buf, buf + size, val, 8);
  CF_EXPECT(result.ec == std::errc(), "Failed to parse octal value");
  return val;
}

uint16_t ReadUnaligned16(const uint8_t ptr[2], bool file_is_big_endian) {
  uint16_t val;
  memcpy(&val, ptr, sizeof(val));  // Guarantees alignment of `val`
  return file_is_big_endian ? be16toh(val) : le16toh(val);
}

uint32_t ReadUnaligned32(const uint8_t ptr[4], bool file_is_big_endian) {
  uint32_t val;
  memcpy(&val, ptr, sizeof(val));  // Guarantees alignment of `val`
  return file_is_big_endian ? be32toh(val) : le32toh(val);
}

}  // namespace

Result<std::unique_ptr<CpioReader>> CpioReader::Open(
    std::unique_ptr<ReaderSeeker> reader) {
  CF_EXPECT(reader != nullptr, "Reader is null");
  EntriesMap entries = CF_EXPECT(Parse(*reader));
  return std::unique_ptr<CpioReader>(
      new CpioReader(std::move(reader), std::move(entries)));
}

CpioReader::CpioReader(std::unique_ptr<ReaderSeeker> reader, EntriesMap entries)
    : reader_(std::move(reader)), entries_(std::move(entries)) {}

Result<CpioReader::EntriesMap> CpioReader::Parse(const ReaderSeeker& reader) {
  std::string magic(6, '\0');
  CF_EXPECT(PReadExact(reader, magic.data(), magic.size(), 0));

  if (magic.starts_with(kBinaryLeMagic)) {
    return CF_EXPECT(ParseBin(reader, /* file_is_big_endian= */ false));
  } else if (magic.starts_with(kBinaryBeMagic)) {
    return CF_EXPECT(ParseBin(reader, /* file_is_big_endian= */ true));
  } else if (magic == kMagicNewc1 || magic == kMagicNewc2) {
    return CF_EXPECT(ParseNewc(reader));
  } else if (magic == kMagicOdc) {
    return CF_EXPECT(ParseOdc(reader));
  } else {
    return CF_ERRF("Unknown cpio magic: '{}'", magic);
  }
}

Result<CpioReader::EntriesMap> CpioReader::ParseNewc(const ReaderSeeker& reader) {
  EntriesMap entries;
  uint64_t offset = 0;
  while (true) {
    // Align to 4 bytes before reading header
    offset = AlignToPowerOf2(offset, 2);

    CpioNewcHeader header;
    CF_EXPECT(PReadExact(reader, reinterpret_cast<char*>(&header),
                         sizeof(header), offset));

    std::string_view magic(header.magic, sizeof(header.magic));
    CF_EXPECT(magic == kMagicNewc1 || magic == kMagicNewc2,
              "Invalid magic in header");

    uint32_t filesize =
        CF_EXPECT(ParseHex(header.filesize, sizeof(header.filesize)));
    uint32_t namesize =
        CF_EXPECT(ParseHex(header.namesize, sizeof(header.namesize)));
    uint32_t mode = CF_EXPECT(ParseHex(header.mode, sizeof(header.mode)));

    std::string name(namesize - 1, '\0');
    CF_EXPECT(PReadExact(reader, name.data(), namesize - 1,
                         offset + offsetof(CpioNewcHeader, filename)));

    if (name == kTrailerName) {
      break;
    }

    uint64_t data_offset = AlignToPowerOf2(
        offset + offsetof(CpioNewcHeader, filename) + namesize, 2);

    entries.emplace(std::move(name), FileEntry{
                                         .offset = data_offset,
                                         .size = filesize,
                                         .mode = mode,
                                     });

    offset = data_offset + filesize;
  }
  return entries;
}

Result<CpioReader::EntriesMap> CpioReader::ParseOdc(const ReaderSeeker& reader) {
  EntriesMap entries;
  uint64_t offset = 0;
  while (true) {
    CpioOdcHeader header;
    CF_EXPECT(PReadExact(reader, reinterpret_cast<char*>(&header),
                         sizeof(header), offset));

    std::string_view magic(header.magic, sizeof(header.magic));
    CF_EXPECT_EQ(magic, kMagicOdc, "Invalid magic in header");

    uint32_t mode = CF_EXPECT(ParseOctal(header.mode, sizeof(header.mode)));
    uint32_t namesize =
        CF_EXPECT(ParseOctal(header.namesize, sizeof(header.namesize)));
    uint32_t filesize =
        CF_EXPECT(ParseOctal(header.filesize, sizeof(header.filesize)));

    std::string name(namesize - 1, '\0');
    CF_EXPECT(PReadExact(reader, name.data(), namesize - 1,
                         offset + offsetof(CpioOdcHeader, filename)));

    if (name == kTrailerName) {
      break;
    }

    uint64_t data_offset =
        offset + offsetof(CpioOdcHeader, filename) + namesize;

    entries.emplace(std::move(name), FileEntry{
                                         .offset = data_offset,
                                         .size = filesize,
                                         .mode = mode,
                                     });

    offset = data_offset + filesize;
  }
  return entries;
}

Result<CpioReader::EntriesMap> CpioReader::ParseBin(const ReaderSeeker& reader,
                                                    bool file_is_big_endian) {
  EntriesMap entries;
  uint64_t offset = 0;
  while (true) {
    CpioBinHeader header;
    CF_EXPECT(PReadExact(reader, reinterpret_cast<char*>(&header),
                         offsetof(CpioBinHeader, filename), offset));

    std::string_view header_magic(reinterpret_cast<const char*>(&header.magic),
                                  sizeof(header.magic));
    CF_EXPECT_EQ(header_magic,
                 file_is_big_endian ? kBinaryBeMagic : kBinaryLeMagic);

    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&header);

    uint16_t mode = ReadUnaligned16(
        header_bytes + offsetof(CpioBinHeader, mode), file_is_big_endian);
    uint16_t namesize = ReadUnaligned16(
        header_bytes + offsetof(CpioBinHeader, namesize), file_is_big_endian);
    uint32_t filesize = ReadUnaligned32(
        header_bytes + offsetof(CpioBinHeader, filesize), file_is_big_endian);

    std::string name(namesize - 1, '\0');
    CF_EXPECT(PReadExact(reader, name.data(), namesize - 1,
                         offset + offsetof(CpioBinHeader, filename)));

    if (name == kTrailerName) {
      break;
    }

    uint64_t data_offset = AlignToPowerOf2(
        offset + offsetof(CpioBinHeader, filename) + namesize, 1);

    entries.emplace(std::move(name), FileEntry{
                                         .offset = data_offset,
                                         .size = filesize,
                                         .mode = mode,
                                     });

    offset = AlignToPowerOf2(data_offset + filesize, 1);
  }
  return entries;
}

Result<std::unique_ptr<ReaderSeeker>> CpioReader::OpenReadOnly(
    std::string_view path) {
  auto it = entries_.find(path);
  CF_EXPECTF(it != entries_.end(), "File not found in cpio: '{}'", path);
  return std::make_unique<ReadWindowView>(*reader_, it->second.offset,
                                          it->second.size);
}

Result<uint32_t> CpioReader::FileAttributes(std::string_view path) const {
  auto it = entries_.find(path);
  CF_EXPECTF(it != entries_.end(), "File not found in cpio: '{}'", path);
  return it->second.mode;
}

}  // namespace cuttlefish
