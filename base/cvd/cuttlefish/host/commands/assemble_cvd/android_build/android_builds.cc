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

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_builds.h"

#include <stddef.h>

#include <memory>
#include <ostream>
#include <utility>
#include <vector>

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<AndroidBuilds> AndroidBuilds::Create(
    std::vector<std::unique_ptr<AndroidBuild>> providers) {
  CF_EXPECT(!providers.empty());
  for (const auto& provider : providers) {
    CF_EXPECT(provider.get());
  }
  return AndroidBuilds(std::move(providers));
}

AndroidBuilds::AndroidBuilds(
    std::vector<std::unique_ptr<AndroidBuild>> providers)
    : providers_(std::move(providers)) {}

AndroidBuild& AndroidBuilds::ForIndex(size_t index) {
  const AndroidBuilds& const_this = *this;
  return const_cast<AndroidBuild&>(const_this.ForIndex(index));
}

const AndroidBuild& AndroidBuilds::ForIndex(size_t index) const {
  if (index < providers_.size()) {
    return *providers_[index];
  } else {
    return *providers_[0];
  }
}

std::ostream& operator<<(std::ostream& out, const AndroidBuilds& providers) {
  out << "AndroidBuilds {";
  for (const auto& provider : providers.providers_) {
    out << *provider << ", ";
  }
  return out << "}";
};

}  // namespace cuttlefish
