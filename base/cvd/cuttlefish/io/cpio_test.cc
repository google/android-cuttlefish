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

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include "cuttlefish/io/in_memory.h"
#include "cuttlefish/io/read_exact.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(CpioReaderTest, ReadNewc) {
  // Construct a valid newc cpio archive in memory
  std::string archive;

  // File 1 header
  archive += "070701";    // magic
  archive += "00000001";  // ino
  archive += "000081a4";  // mode (reg file, 0644)
  archive += "00000000";  // uid
  archive += "00000000";  // gid
  archive += "00000001";  // nlink
  archive += "00000000";  // mtime
  archive += "0000000c";  // filesize (12)
  archive += "00000000";  // maj
  archive += "00000000";  // min
  archive += "00000000";  // rmaj
  archive += "00000000";  // rmin
  archive += "00000006";  // namesize (6)
  archive += "00000000";  // chksum
  archive += "file1";
  archive += '\0';  // null terminator
  // 110 + 6 = 116 (no padding needed)
  archive += "Hello World\n";
  // 12 bytes data (no padding needed)

  // Trailer
  archive += "070701";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "0000000b";  // namesize (11)
  archive += "00000000";
  archive += "TRAILER!!!";
  archive += '\0';
  // 110 + 11 = 121. Pad to 124.
  archive += std::string(3, '\0');

  std::unique_ptr<ReaderWriterSeeker> io = InMemoryIo(archive);
  Result<std::unique_ptr<CpioReader>> reader_result =
      CpioReader::Open(std::move(io));
  ASSERT_THAT(reader_result, IsOk());
  std::unique_ptr<CpioReader> reader = std::move(*reader_result);

  Result<std::unique_ptr<ReaderSeeker>> file_result =
      reader->OpenReadOnly("file1");
  ASSERT_THAT(file_result, IsOk());
  std::unique_ptr<ReaderSeeker> file = std::move(*file_result);

  EXPECT_THAT(ReadToString(*file), IsOkAndValue("Hello World\n"));

  EXPECT_THAT(reader->FileAttributes("file1"), IsOkAndValue(0x81a4));
}

TEST(CpioReaderTest, ReadOdc) {
  // Construct a valid odc cpio archive in memory
  std::string archive;

  // File 1 header
  archive += "070707";       // magic
  archive += "000001";       // dev
  archive += "000001";       // ino
  archive += "100644";       // mode (octal for reg file 0644)
  archive += "000000";       // uid
  archive += "000000";       // gid
  archive += "000001";       // nlink
  archive += "000000";       // rdev
  archive += "00000000000";  // mtime (11 bytes)
  archive += "000006";       // namesize (6)
  archive += "00000000014";  // filesize (12 in octal? Wait, 12 is 014 in octal)
  archive += "file1";
  archive += '\0';
  archive += "Hello World\n";

  // Trailer
  archive += "070707";
  archive += "000000";
  archive += "000000";
  archive += "000000";
  archive += "000000";
  archive += "000000";
  archive += "000000";
  archive += "000000";
  archive += "00000000000";
  archive += "000013";       // namesize (11 in octal is 013)
  archive += "00000000000";  // filesize (0)
  archive += "TRAILER!!!";
  archive += '\0';

  std::unique_ptr<ReaderWriterSeeker> io = InMemoryIo(archive);
  Result<std::unique_ptr<CpioReader>> reader_result =
      CpioReader::Open(std::move(io));
  ASSERT_THAT(reader_result, IsOk());
  std::unique_ptr<CpioReader> reader = std::move(*reader_result);

  Result<std::unique_ptr<ReaderSeeker>> file_result =
      reader->OpenReadOnly("file1");
  ASSERT_THAT(file_result, IsOk());
  std::unique_ptr<ReaderSeeker> file = std::move(*file_result);

  EXPECT_THAT(ReadToString(*file), IsOkAndValue("Hello World\n"));

  EXPECT_THAT(reader->FileAttributes("file1"), IsOkAndValue(0100644));
}

TEST(CpioReaderTest, ReadBin) {
  // Construct a valid binary cpio archive in memory
  std::vector<char> archive;

  auto push16 = [&](uint16_t val) {
    archive.push_back(val & 0xff);
    archive.push_back((val >> 8) & 0xff);
  };

  auto push32 = [&](uint32_t val) {
    archive.push_back(val & 0xff);
    archive.push_back((val >> 8) & 0xff);
    archive.push_back((val >> 16) & 0xff);
    archive.push_back((val >> 24) & 0xff);
  };

  // File 1 header
  push16(0x71C7);  // magic
  push16(0);       // dev
  push16(1);       // ino
  push16(0x81a4);  // mode
  push16(0);       // uid
  push16(0);       // gid
  push16(1);       // nlink
  push16(0);       // rdev
  push32(0);       // mtime
  push16(6);       // namesize
  push32(12);      // filesize

  std::string name = "file1";
  archive.insert(archive.end(), name.begin(), name.end());
  archive.push_back('\0');

  std::string data = "Hello World\n";
  archive.insert(archive.end(), data.begin(), data.end());

  // Trailer
  push16(0x71C7);
  push16(0);
  push16(0);
  push16(0);
  push16(0);
  push16(0);
  push16(0);
  push16(0);
  push32(0);
  push16(11);  // namesize
  push32(0);

  std::string trailer = "TRAILER!!!";
  archive.insert(archive.end(), trailer.begin(), trailer.end());
  archive.push_back('\0');
  archive.push_back('\0');  // Pad to 2 bytes (37 + 1 = 38)

  std::unique_ptr<ReaderWriterSeeker> io =
      InMemoryIo(std::string_view(archive.data(), archive.size()));
  Result<std::unique_ptr<CpioReader>> reader_result =
      CpioReader::Open(std::move(io));
  ASSERT_THAT(reader_result, IsOk());
  std::unique_ptr<CpioReader> reader = std::move(*reader_result);

  Result<std::unique_ptr<ReaderSeeker>> file_result =
      reader->OpenReadOnly("file1");
  ASSERT_THAT(file_result, IsOk());
  std::unique_ptr<ReaderSeeker> file = std::move(*file_result);

  EXPECT_THAT(ReadToString(*file), IsOkAndValue("Hello World\n"));

  EXPECT_THAT(reader->FileAttributes("file1"), IsOkAndValue(0x81a4));
}

TEST(CpioReaderTest, ReadMultipleFiles) {
  std::string archive;

  // File 1 header
  archive += "070701";    // magic
  archive += "00000001";  // ino
  archive += "000081a4";  // mode (reg file, 0644)
  archive += "00000000";  // uid
  archive += "00000000";  // gid
  archive += "00000001";  // nlink
  archive += "00000000";  // mtime
  archive += "0000000c";  // filesize (12)
  archive += "00000000";  // maj
  archive += "00000000";  // min
  archive += "00000000";  // rmaj
  archive += "00000000";  // rmin
  archive += "00000006";  // namesize (6)
  archive += "00000000";  // chksum
  archive += "file1";
  archive += '\0';  // null terminator
  archive += "Hello World\n";

  // File 2 header
  archive += "070701";    // magic
  archive += "00000002";  // ino
  archive += "000081a4";  // mode (reg file, 0644)
  archive += "00000000";  // uid
  archive += "00000000";  // gid
  archive += "00000001";  // nlink
  archive += "00000000";  // mtime
  archive += "0000000e";  // filesize (14)
  archive += "00000000";  // maj
  archive += "00000000";  // min
  archive += "00000000";  // rmaj
  archive += "00000000";  // rmin
  archive += "00000006";  // namesize (6)
  archive += "00000000";  // chksum
  archive += "file2";
  archive += '\0';  // null terminator
  archive += "Goodbye World\n";
  archive += std::string(2, '\0');  // Pad data to 4 bytes (14 + 2 = 16)

  // Trailer
  archive += "070701";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "00000000";
  archive += "0000000b";  // namesize (11)
  archive += "00000000";
  archive += "TRAILER!!!";
  archive += '\0';
  archive += std::string(3, '\0');  // Pad to 4 bytes (110 + 11 = 121 -> 124)

  std::unique_ptr<ReaderWriterSeeker> io = InMemoryIo(archive);
  Result<std::unique_ptr<CpioReader>> reader_result =
      CpioReader::Open(std::move(io));
  ASSERT_THAT(reader_result, IsOk());
  std::unique_ptr<CpioReader> reader = std::move(*reader_result);

  // Verify File 1
  Result<std::unique_ptr<ReaderSeeker>> file1_result =
      reader->OpenReadOnly("file1");
  ASSERT_THAT(file1_result, IsOk());
  std::unique_ptr<ReaderSeeker> file1 = std::move(*file1_result);
  EXPECT_THAT(ReadToString(*file1), IsOkAndValue("Hello World\n"));

  // Verify File 2
  Result<std::unique_ptr<ReaderSeeker>> file2_result =
      reader->OpenReadOnly("file2");
  ASSERT_THAT(file2_result, IsOk());
  std::unique_ptr<ReaderSeeker> file2 = std::move(*file2_result);
  EXPECT_THAT(ReadToString(*file2), IsOkAndValue("Goodbye World\n"));
}

}  // namespace
}  // namespace cuttlefish
