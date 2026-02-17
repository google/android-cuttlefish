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

#include "cuttlefish/host/commands/assemble_cvd/android_build/fake_android_build.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/str_cat.h"

#include "cuttlefish/pretty/map.h"       // IWYU pragma: keep
#include "cuttlefish/pretty/optional.h"  // IWYU pragma: keep
#include "cuttlefish/pretty/pair.h"      // IWYU pragma: keep
#include "cuttlefish/pretty/set.h"       // IWYU pragma: keep
#include "cuttlefish/pretty/struct.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

std::string FakeAndroidBuild::Name() const { return "FakeAndroidBuild"; }

PrettyStruct FakeAndroidBuild::Pretty() {
  return PrettyStruct("FakeAndroidBuild")
      .Member("images_", images_)
      .Member("extract_dir_", extract_dir_)
      .Member("ab_partitions_", ab_partitions_)
      .Member("system_partitions_", system_partitions_)
      .Member("vendor_partitions_", vendor_partitions_)
      .Member("logical_partitions_", logical_partitions_)
      .Member("physical_partitions_", physical_partitions_);
}

Result<std::set<std::string, std::less<void>>> FakeAndroidBuild::Images() {
  CF_EXPECT(images_.has_value());
  std::set<std::string, std::less<void>> keys;
  for (const auto& [key, unused_value] : *images_) {
    keys.insert(key);
  }
  return keys;
}

Result<std::string> FakeAndroidBuild::ImageFile(std::string_view name,
                                                bool extract) {
  CF_EXPECT(images_.has_value());
  auto it = images_->find(name);
  CF_EXPECT(it != images_->end());
  if (it->second.first == ImageStatus::kUnextracted) {
    CF_EXPECT(!!extract);
    CF_EXPECT(extract_dir_.has_value());
    it->second = {ImageStatus::kExtracted,
                  absl::StrCat(*extract_dir_, "/", name)};
  }
  if (it->second.first == ImageStatus::kExtracted) {
    return it->second.second;
  }
  return CF_ERR("missing");
}

void FakeAndroidBuild::AddExtractedImage(std::string_view image,
                                         std::string_view path) {
  if (!images_.has_value()) {
    images_ = {{}};
  }
  (*images_)[std::string(image)] = {ImageStatus::kExtracted, std::string(path)};
}

void FakeAndroidBuild::AddUnextractedImage(std::string_view image) {
  if (!images_.has_value()) {
    images_ = {{}};
  }
  (*images_)[std::string(image)] = {ImageStatus::kUnextracted, ""};
}

void FakeAndroidBuild::AddMissingImage(std::string_view image) {
  if (!images_.has_value()) {
    images_ = {{}};
  }
  (*images_)[std::string(image)] = {ImageStatus::kMissing, ""};
}

Result<void> FakeAndroidBuild::SetExtractDir(std::string_view extract_dir) {
  extract_dir_ = extract_dir;
  return {};
}

Result<std::set<std::string, std::less<void>>>
FakeAndroidBuild::AbPartitions() {
  CF_EXPECT(ab_partitions_.has_value());
  return *ab_partitions_;
}

void FakeAndroidBuild::SetAbPartitions(
    std::set<std::string, std::less<void>> partitions) {
  ab_partitions_ = std::move(partitions);
}

Result<std::set<std::string, std::less<void>>>
FakeAndroidBuild::SystemPartitions() {
  CF_EXPECT(system_partitions_.has_value());
  return *system_partitions_;
}

void FakeAndroidBuild::SetSystemPartitions(
    std::set<std::string, std::less<void>> partitions) {
  system_partitions_ = std::move(partitions);
}

Result<std::set<std::string, std::less<void>>>
FakeAndroidBuild::VendorPartitions() {
  CF_EXPECT(vendor_partitions_.has_value());
  return *vendor_partitions_;
}

void FakeAndroidBuild::SetVendorPartitions(
    std::set<std::string, std::less<void>> partitions) {
  vendor_partitions_ = std::move(partitions);
}

Result<std::set<std::string, std::less<void>>>
FakeAndroidBuild::LogicalPartitions() {
  CF_EXPECT(logical_partitions_.has_value());
  return *logical_partitions_;
}

void FakeAndroidBuild::SetLogicalPartitions(
    std::set<std::string, std::less<void>> partitions) {
  logical_partitions_ = std::move(partitions);
}

Result<std::set<std::string, std::less<void>>>
FakeAndroidBuild::PhysicalPartitions() {
  CF_EXPECT(physical_partitions_.has_value());
  return *physical_partitions_;
}

void FakeAndroidBuild::SetPhysicalPartitions(
    std::set<std::string, std::less<void>> partitions) {
  physical_partitions_ = std::move(partitions);
}

std::string_view ImageStatusToStringView(
    FakeAndroidBuild::ImageStatus image_status) {
  switch (image_status) {
    case FakeAndroidBuild::ImageStatus::kExtracted:
      return "FakeAndroidBuild::ImageStatus::kExtracted";
    case FakeAndroidBuild::ImageStatus::kUnextracted:
      return "FakeAndroidBuild::ImageStatus::kUnextracted";
    case FakeAndroidBuild::ImageStatus::kMissing:
      return "FakeAndroidBuild::ImageStatus::kMissing";
  }
}

}  // namespace cuttlefish
