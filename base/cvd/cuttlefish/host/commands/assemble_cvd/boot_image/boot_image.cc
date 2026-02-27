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

#include <map>
#include <string>
#include <utility>
#include <variant>

#include "absl/strings/str_cat.h"
#include "bootimg.h"

#include "cuttlefish/io/copy.h"
#include "cuttlefish/io/filesystem.h"
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
  const auto visitor = [](const auto& hdr) {
    return KernelCommandLineImpl(hdr);
  };
  return std::visit(visitor, header_);
}

static uint32_t PageSizeImpl(const boot_img_hdr_v0& v0) { return v0.page_size; }
static uint32_t PageSizeImpl(const boot_img_hdr_v3&) { return 4096; }

uint32_t BootImage::PageSize() const {
  const auto visitor = [](const auto& hdr) { return PageSizeImpl(hdr); };
  return std::visit(visitor, header_);
}

static uint32_t KernelSize(const boot_img_hdr_v0& v0) { return v0.kernel_size; }
static uint32_t KernelSize(const boot_img_hdr_v3& v3) { return v3.kernel_size; }

uint64_t BootImage::KernelPages() const {
  const auto visitor = [](const auto& hdr) { return KernelSize(hdr); };
  const uint64_t kernel_size = std::visit(visitor, header_);
  return (kernel_size + PageSize() - 1) / PageSize();
}

ReadWindowView BootImage::Kernel() const {
  const auto visitor = [](const auto& hdr) { return KernelSize(hdr); };
  const uint64_t kernel_size = std::visit(visitor, header_);
  return ReadWindowView(*reader_, PageSize(), kernel_size);
}

static uint32_t RamdiskSize(const boot_img_hdr_v0& v0) {
  return v0.ramdisk_size;
}
static uint32_t RamdiskSize(const boot_img_hdr_v3& v3) {
  return v3.ramdisk_size;
}

uint64_t BootImage::RamdiskPages() const {
  const auto visitor = [](const auto& hdr) { return RamdiskSize(hdr); };
  const uint64_t ramdisk_size = std::visit(visitor, header_);
  return (ramdisk_size + PageSize() - 1) / PageSize();
}

ReadWindowView BootImage::Ramdisk() const {
  const uint64_t start = (1 + KernelPages()) * PageSize();
  const auto visitor = [](const auto& hdr) { return RamdiskSize(hdr); };
  const uint64_t ramdisk_size = std::visit(visitor, header_);
  return ReadWindowView(*reader_, start, ramdisk_size);
}

std::optional<ReadWindowView> BootImage::Signature() const {
  const boot_img_hdr_v4* v4 = std::get_if<boot_img_hdr_v4>(&header_);
  if (v4 == nullptr) {
    return std::nullopt;
  }
  const uint64_t start = (1 + KernelPages() + RamdiskPages()) * PageSize();
  return ReadWindowView(*reader_, start, v4->signature_size);
}

Result<void> BootImage::Unpack(ReadWriteFilesystem& fs) {
  std::map<std::string_view, ReadWindowView> files = {
      {"/kernel", Kernel()},
      {"/ramdisk", Ramdisk()},
  };
  if (std::optional<ReadWindowView> signature = Signature(); signature) {
    files.emplace("/boot_signature", std::move(*signature));
  }
  for (auto& [target, source] : files) {
    Result<void> unused = fs.DeleteFile(target);
    std::unique_ptr<ReaderWriterSeeker> target_out =
        CF_EXPECTF(fs.CreateFile(target), "Failed to create '{}'.", target);
    CF_EXPECTF(target_out.get(), "Failed to create '{}'", target);
    CF_EXPECTF(Copy(source, *target_out), "Failed to write '{}'.", target);
  }
  return {};
}

}  // namespace cuttlefish
