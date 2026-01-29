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

#include "cuttlefish/host/commands/assemble_cvd/android_build/physical_partitions.h"

#include <functional>
#include <memory>
#include <set>
#include <string>

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/pretty/result.h"  // IWYU pragma: keep: overloads
#include "cuttlefish/pretty/set.h"     // IWYU pragma: keep: overloads
#include "cuttlefish/pretty/struct.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

class PhysicalPartitionsImpl : public AndroidBuild {
 public:
  PhysicalPartitionsImpl(AndroidBuild& build) : build_(build) {}

  std::string Name() const override { return "PhysicalPartitions"; }

  PrettyStruct Pretty() override {
    return PrettyStruct(Name())
        .Member("PhysicalPartitions()", PhysicalPartitions());
  }

  Result<std::set<std::string, std::less<void>>> PhysicalPartitions() override {
    if (auto res = build_.PhysicalPartitions(); res.ok()) {
      return *res;
    }

    std::set<std::string, std::less<void>> partitions =
        CF_EXPECT(build_.Images());
    for (std::string logical : CF_EXPECT(build_.LogicalPartitions())) {
      partitions.erase(logical);
    }

    if (partitions.count("super_empty")) {
      partitions.erase("super_empty");
      partitions.insert("super");
    }

    return partitions;
  }

 private:
  AndroidBuild& build_;
};

}  // namespace

Result<std::unique_ptr<AndroidBuild>> PhysicalPartitions(AndroidBuild& build) {
  auto partitions = std::make_unique<PhysicalPartitionsImpl>(build);

  CF_EXPECT(partitions->PhysicalPartitions());

  return partitions;
}

}  // namespace cuttlefish
