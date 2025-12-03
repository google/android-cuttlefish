//
// Copyright (C) 2025 The Android Open Source Project
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
#include <vector>

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class AndroidBuilds {
 public:
  static Result<AndroidBuilds> Create(
      std::vector<std::unique_ptr<AndroidBuild>>);

  AndroidBuild& ForIndex(size_t index);
  const AndroidBuild& ForIndex(size_t index) const;

  friend std::ostream& operator<<(std::ostream&, const AndroidBuilds&);

 private:
  AndroidBuilds(std::vector<std::unique_ptr<AndroidBuild>>);

  std::vector<std::unique_ptr<AndroidBuild>> providers_;
};

}  // namespace cuttlefish

namespace fmt {

template <>
struct formatter<::cuttlefish::AndroidBuilds> : ostream_formatter {};

}  // namespace fmt
