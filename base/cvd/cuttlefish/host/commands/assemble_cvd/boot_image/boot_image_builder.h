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

#pragma once

#include <memory>
#include <string>

#include "cuttlefish/io/concat.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class BootImageBuilder {
 public:
  BootImageBuilder& KernelCommandLine(std::string_view) &;
  BootImageBuilder KernelCommandLine(std::string_view) &&;

  BootImageBuilder& Kernel(std::unique_ptr<ReaderSeeker>) &;
  BootImageBuilder Kernel(std::unique_ptr<ReaderSeeker>) &&;

  BootImageBuilder& Ramdisk(std::unique_ptr<ReaderSeeker>) &;
  BootImageBuilder Ramdisk(std::unique_ptr<ReaderSeeker>) &&;

  BootImageBuilder& Signature(std::unique_ptr<ReaderSeeker>) &;
  BootImageBuilder Signature(std::unique_ptr<ReaderSeeker>) &&;

  BootImageBuilder& OsVersion(uint32_t) &;
  BootImageBuilder OsVersion(uint32_t) &&;

  Result<ConcatReaderSeeker> BuildV4();

 private:
  std::string kernel_command_line_;
  std::unique_ptr<ReaderSeeker> kernel_;
  std::unique_ptr<ReaderSeeker> ramdisk_;
  std::unique_ptr<ReaderSeeker> signature_;
  uint32_t os_version_ = 0;
};

}  // namespace cuttlefish
