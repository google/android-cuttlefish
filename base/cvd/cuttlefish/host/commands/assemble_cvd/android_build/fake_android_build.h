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

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/pretty/struct.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class FakeAndroidBuild : public AndroidBuild {
 public:
  enum class ImageStatus {
    kExtracted,
    kUnextracted,
    kMissing,
  };

  /** The name of the concrete implementation. **/
  std::string Name() const override;

  PrettyStruct Pretty() override;

  Result<std::set<std::string, std::less<void>>> Images() override;
  Result<std::string> ImageFile(std::string_view name,
                                bool extract = true) override;
  void AddExtractedImage(std::string_view image, std::string_view path);
  void AddUnextractedImage(std::string_view image);
  void AddMissingImage(std::string_view image);

  Result<void> SetExtractDir(std::string_view) override;

  Result<std::set<std::string, std::less<void>>> AbPartitions() override;
  void SetAbPartitions(std::set<std::string, std::less<void>>);

  Result<std::set<std::string, std::less<void>>> SystemPartitions() override;
  void SetSystemPartitions(std::set<std::string, std::less<void>>);

  Result<std::set<std::string, std::less<void>>> VendorPartitions() override;
  void SetVendorPartitions(std::set<std::string, std::less<void>>);

  Result<std::set<std::string, std::less<void>>> LogicalPartitions() override;
  void SetLogicalPartitions(std::set<std::string, std::less<void>>);

  Result<std::set<std::string, std::less<void>>> PhysicalPartitions() override;
  void SetPhysicalPartitions(std::set<std::string, std::less<void>>);

 private:
  // An unset value means that calling the getter should return an error.
  std::optional<std::map<std::string, std::pair<ImageStatus, std::string>,
                         std::less<void>>>
      images_;
  std::optional<std::string> extract_dir_;
  std::optional<std::set<std::string, std::less<void>>> ab_partitions_;
  std::optional<std::set<std::string, std::less<void>>> system_partitions_;
  std::optional<std::set<std::string, std::less<void>>> vendor_partitions_;
  std::optional<std::set<std::string, std::less<void>>> logical_partitions_;
  std::optional<std::set<std::string, std::less<void>>> physical_partitions_;
};

std::string_view ImageStatusToStringView(FakeAndroidBuild::ImageStatus);

template <typename Sink>
void AbslStringify(Sink& sink, FakeAndroidBuild::ImageStatus image_status) {
  sink.Append(ImageStatusToStringView(image_status));
}

}  // namespace cuttlefish
