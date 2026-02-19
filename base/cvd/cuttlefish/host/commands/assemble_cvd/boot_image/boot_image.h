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

#include <variant>

#include "bootimg.h"

#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class BootImage {
 public:
  static Result<BootImage> Read(std::unique_ptr<ReaderSeeker>);

  std::string KernelCommandLine() const;

 private:
  using HeaderVariant =
      std::variant<boot_img_hdr_v0, boot_img_hdr_v1, boot_img_hdr_v2,
                   boot_img_hdr_v3, boot_img_hdr_v4>;

  BootImage(std::unique_ptr<ReaderSeeker>, HeaderVariant);

  std::unique_ptr<ReaderSeeker> reader_;
  HeaderVariant header_;
};

}  // namespace cuttlefish
