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

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/identify_build.h"
#include "cuttlefish/pretty/pretty.h"
#include "cuttlefish/pretty/struct.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class AndroidBuilds {
 public:
  static Result<AndroidBuilds> Identify(std::vector<AndroidBuildKey>);

  AndroidBuild& ForIndex(size_t index);
  const AndroidBuild& ForIndex(size_t index) const;

  size_t Size() const;

  // For libfmt
  friend std::string format_as(const AndroidBuilds&);

  friend PrettyStruct Pretty(AndroidBuilds&, PrettyAdlPlaceholder);

 private:
  AndroidBuilds() = default;

  std::vector<AndroidBuildKey> keys_;
  std::map<AndroidBuildKey, std::unique_ptr<AndroidBuild>> builds_;
};

std::ostream& operator<<(std::ostream&, const AndroidBuilds&);

PrettyStruct Pretty(AndroidBuilds&,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

}  // namespace cuttlefish
