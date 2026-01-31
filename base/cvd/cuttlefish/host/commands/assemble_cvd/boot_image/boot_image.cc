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

#include "cuttlefish/host/commands/assemble_cvd/boot_image/boot_image.h"

#include <stdint.h>

#include <string>
#include <string_view>
#include <variant>

#include "absl/strings/str_cat.h"
#include "bootimg.h"

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// https://source.android.com/docs/core/architecture/bootloader/boot-image-header

Result<BootImage> BootImage::Read(std::string_view path) {
  const SharedFD fd = SharedFD::Open(std::string(path), O_RDONLY);
  CF_EXPECTF(fd->IsOpen(), "Failed to open '{}': '{}'", path, fd->StrError());

  std::string magic(BOOT_MAGIC_SIZE, ' ');
  CF_EXPECT_EQ(ReadExact(fd, &magic), BOOT_MAGIC_SIZE, fd->StrError());
  CF_EXPECT_EQ(magic, BOOT_MAGIC);

  const off_t version_off = BOOT_MAGIC_SIZE + (sizeof(uint32_t) * 8);
  CF_EXPECT_EQ(fd->LSeek(version_off, SEEK_SET), version_off, fd->StrError());
  uint32_t version;
  CF_EXPECT_EQ(ReadExactBinary(fd, &version), sizeof(version), fd->StrError());

  CF_EXPECT_EQ(fd->LSeek(SEEK_SET, 0), 0, fd->StrError());

  switch (version) {
    case 0: {
      boot_img_hdr_v0 v0;
      CF_EXPECT_EQ(ReadExactBinary(fd, &v0), sizeof(v0), fd->StrError());
      return BootImage(fd, v0);
    }
    case 1: {
      boot_img_hdr_v1 v1;
      CF_EXPECT_EQ(ReadExactBinary(fd, &v1), sizeof(v1), fd->StrError());
      return BootImage(fd, v1);
    }
    case 2: {
      boot_img_hdr_v2 v2;
      CF_EXPECT_EQ(ReadExactBinary(fd, &v2), sizeof(v2), fd->StrError());
      return BootImage(fd, v2);
    }
    case 3: {
      boot_img_hdr_v3 v3;
      CF_EXPECT_EQ(ReadExactBinary(fd, &v3), sizeof(v3), fd->StrError());
      return BootImage(fd, v3);
    }
    case 4: {
      boot_img_hdr_v4 v4;
      CF_EXPECT_EQ(ReadExactBinary(fd, &v4), sizeof(v4), fd->StrError());
      return BootImage(fd, v4);
    }
    default:
      return CF_ERRF("Unknown header version {}", version);
  }
}

BootImage::BootImage(SharedFD fd, HeaderVariant header)
    : fd_(fd), header_(header) {}

static std::string KernelCommandLineImpl(const boot_img_hdr_v0& v0) {
  const char* cmdline = reinterpret_cast<const char*>(v0.cmdline);
  const char* extra_cmdline(reinterpret_cast<const char*>(v0.extra_cmdline));
  return absl::StrCat(cmdline, extra_cmdline);
}

static std::string KernelCommandLineImpl(const boot_img_hdr_v3& v3) {
  return std::string(reinterpret_cast<const char*>(v3.cmdline));
}

std::string BootImage::KernelCommandLine() const {
  auto visitor = [](const auto& hdr) { return KernelCommandLineImpl(hdr); };
  return std::visit(visitor, header_);
}

}  // namespace cuttlefish
