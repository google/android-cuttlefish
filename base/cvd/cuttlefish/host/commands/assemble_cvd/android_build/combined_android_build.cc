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

#include "cuttlefish/host/commands/assemble_cvd/android_build/combined_android_build.h"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/pretty/result.h"  // IWYU pragma: keep: overloads
#include "cuttlefish/pretty/set.h"     // IWYU pragma: keep: overloads
#include "cuttlefish/pretty/struct.h"
#include "cuttlefish/pretty/unique_ptr.h"  // IWYU pragma: keep: overloads
#include "cuttlefish/pretty/vector.h"      // IWYU pragma: keep: overloads
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

class CombinedAndroidBuildImpl : public AndroidBuild {
 public:
  CombinedAndroidBuildImpl(std::string name,
                           std::vector<std::unique_ptr<AndroidBuild>> builds)
      : name_(std::move(name)), builds_(std::move(builds)) {}

  std::string Name() const override {
    return absl::StrCat("CombinedAndroidBuild (", name_, ", ", builds_.size(),
                        " members)");
  }

  PrettyStruct Pretty() override {
    return PrettyStruct(Name())
        .Member("Images", Images())
        .Member("AbPartitions()", AbPartitions())
        .Member("SystemPartitions()", SystemPartitions())
        .Member("VendorPartitions()", VendorPartitions())
        .Member("LogicalPartitions()", LogicalPartitions())
        .Member("PhysicalPartitions()", PhysicalPartitions())
        .Member("builds_", builds_);
  }

  Result<std::set<std::string, std::less<void>>> Images() override {
    return CF_EXPECT(MergeSuccessful(&AndroidBuild::Images));
  }

  Result<std::string> ImageFile(std::string_view name, bool extract) override {
    Result<std::string> res;

    // If the file is already extracted somewhere, prefer that version.
    for (const std::unique_ptr<AndroidBuild>& build : builds_) {
      if (res = CF_EXPECT(build.get())->ImageFile(name, false); res.ok()) {
        return res;
      }
    }
    // Now try to extract if it any of the builds have it.
    CF_EXPECTF(!!extract, "'{}' has not been extracted anywhere.", name);
    for (const std::unique_ptr<AndroidBuild>& build : builds_) {
      if (res = build->ImageFile(name, true); res.ok()) {
        return res;
      }
    }
    return CF_ERRF("Could not extract '{}' from {}", name,
                   static_cast<AndroidBuild&>(*this));
  }

  Result<void> SetExtractDir(std::string_view dir) override {
    bool one_succeeded = false;
    Result<void> res;
    for (const std::unique_ptr<AndroidBuild>& build : builds_) {
      res = CF_EXPECT(build.get())->SetExtractDir(dir);
      one_succeeded |= res.ok();
    }
    if (!one_succeeded) {
      CF_EXPECT(std::move(res));
      return CF_ERR("unreachable");
    }
    return {};
  }

  Result<std::set<std::string, std::less<void>>> AbPartitions() override {
    return CF_EXPECT(MergeSuccessful(&AndroidBuild::AbPartitions));
  }

  Result<std::set<std::string, std::less<void>>> SystemPartitions() override {
    return CF_EXPECT(MergeSuccessful(&AndroidBuild::SystemPartitions));
  }

  Result<std::set<std::string, std::less<void>>> VendorPartitions() override {
    return CF_EXPECT(MergeSuccessful(&AndroidBuild::VendorPartitions));
  }

  Result<std::set<std::string, std::less<void>>> LogicalPartitions() override {
    return CF_EXPECT(MergeSuccessful(&AndroidBuild::LogicalPartitions));
  }

  Result<std::set<std::string, std::less<void>>> PhysicalPartitions() override {
    return CF_EXPECT(MergeSuccessful(&AndroidBuild::PhysicalPartitions));
  }

 private:
  Result<std::set<std::string, std::less<void>>> MergeSuccessful(
      Result<std::set<std::string, std::less<void>>> (AndroidBuild::*fn)()) {
    Result<std::set<std::string, std::less<void>>> res = CF_ERR("No members");
    bool one_succeeded = false;

    std::set<std::string, std::less<void>> merged;
    for (const std::unique_ptr<AndroidBuild>& build : builds_) {
      if (res = ((*build).*fn)(); res.ok()) {
        one_succeeded = true;
        merged.merge(std::move(*res));
      }
    }
    return one_succeeded ? merged : res;
  }

  std::string name_;
  std::vector<std::unique_ptr<AndroidBuild>> builds_;
};

}  // namespace

Result<std::unique_ptr<AndroidBuild>> CombinedAndroidBuild(
    std::string name, std::vector<std::unique_ptr<AndroidBuild>> builds) {
  for (auto& build : builds) {
    CF_EXPECT(build.get());
  }
  return std::make_unique<CombinedAndroidBuildImpl>(std::move(name),
                                                    std::move(builds));
}

}  // namespace cuttlefish
