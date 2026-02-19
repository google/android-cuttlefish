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
#include <utility>
#include <variant>

#include "absl/strings/str_cat.h"
#include "bootimg.h"

#include "cuttlefish/io/io.h"
#include "cuttlefish/io/read_exact.h"
#include "cuttlefish/result/result.h"

// https://source.android.com/docs/core/architecture/bootloader/boot-image-header

namespace cuttlefish {

static_assert(sizeof(boot_img_hdr_v2) >= sizeof(boot_img_hdr_v4));
static_assert(sizeof(boot_img_hdr_v2) >= sizeof(boot_img_hdr_v3));
static_assert(sizeof(boot_img_hdr_v2) >= sizeof(boot_img_hdr_v1));
static_assert(sizeof(boot_img_hdr_v2) >= sizeof(boot_img_hdr_v0));

Result<BootImage> BootImage::Read(std::unique_ptr<ReaderSeeker> rd) {
  // `magic` and `header_version` are always in the same place, v2 is largest
  boot_img_hdr_v2 v2 = CF_EXPECT(PReadExactBinary<boot_img_hdr_v2>(*rd, 0));
  CF_EXPECT_EQ(memcmp(v2.magic, BOOT_MAGIC, BOOT_MAGIC_SIZE), 0);

  switch (v2.header_version) {
    case 0:
      return BootImage(std::move(rd), *reinterpret_cast<boot_img_hdr_v0*>(&v2));
    case 1:
      return BootImage(std::move(rd), *reinterpret_cast<boot_img_hdr_v1*>(&v2));
    case 2:
      return BootImage(std::move(rd), v2);
    case 3:
      return BootImage(std::move(rd), *reinterpret_cast<boot_img_hdr_v3*>(&v2));
    case 4:
      return BootImage(std::move(rd), *reinterpret_cast<boot_img_hdr_v4*>(&v2));
    default:
      return CF_ERRF("Unknown header version '{}'", v2.header_version);
  }
}

BootImage::BootImage(std::unique_ptr<ReaderSeeker> reader, HeaderVariant header)
    : reader_(std::move(reader)), header_(header) {}

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
