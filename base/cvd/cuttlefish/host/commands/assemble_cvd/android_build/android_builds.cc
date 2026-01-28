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

#include <map>
#include <memory>
#include <ostream>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h"

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/identify_build.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<AndroidBuilds> AndroidBuilds::Identify(
    std::vector<AndroidBuildKey> keys) {
  CF_EXPECT(!keys.empty());

  std::map<AndroidBuildKey, std::unique_ptr<AndroidBuild>> builds_map;
  for (const AndroidBuildKey& key : keys) {
    if (builds_map.count(key) > 0) {
      continue;
    }
    std::unique_ptr<AndroidBuild> build = CF_EXPECT(IdentifyAndroidBuild(key));
    CF_EXPECT(build.get());
    builds_map[key] = std::move(build);
  }

  CF_EXPECT(!builds_map.empty());

  AndroidBuilds builds;
  builds.keys_ = std::move(keys);
  builds.builds_ = std::move(builds_map);

  return builds;
}

AndroidBuild& AndroidBuilds::ForIndex(const size_t index) {
  const AndroidBuilds& const_this = *this;
  return const_cast<AndroidBuild&>(const_this.ForIndex(index));
}

const AndroidBuild& AndroidBuilds::ForIndex(size_t index) const {
  if (index >= keys_.size()) {
    CHECK(!keys_.empty());
    index = 0;
  }
  const AndroidBuildKey& key = keys_[index];
  auto it = builds_.find(key);
  CHECK(it != builds_.end());
  CHECK(it->second.get() != nullptr);
  return *it->second;
}

size_t AndroidBuilds::Size() const { return keys_.size(); }

std::ostream& operator<<(std::ostream& out, const AndroidBuilds& builds) {
  fmt::print(out, "AndroidBuilds {{ .keys_ = [{}], .builds_= {{",
             fmt::join(builds.keys_, ", "));
  for (const auto& [key, value] : builds.builds_) {
    CHECK(value.get() != nullptr);
    fmt::print(out, "{} -> {}, ", key, *value);
  }
  return out << "}}";
};

}  // namespace cuttlefish
