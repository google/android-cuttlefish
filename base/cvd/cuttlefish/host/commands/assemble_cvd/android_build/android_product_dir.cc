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

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_product_dir.h"

#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

static constexpr std::string_view kImgSuffix = ".img";

class AndroidProductDirImpl : public AndroidBuild {
 public:
  AndroidProductDirImpl(std::string path) : path_(std::move(path)) {}

  Result<std::set<std::string, std::less<void>>> Images() override {
    std::set<std::string, std::less<void>> images;
    for (std::string_view member : CF_EXPECT(DirectoryContents(path_))) {
      if (!absl::ConsumeSuffix(&member, kImgSuffix)) {
        continue;
      }
      images.emplace(member);
    }
    return images;
  }

  Result<std::string> ImageFile(std::string_view name,
                                std::optional<std::string_view>) override {
    std::string image_path = absl::StrCat(path_, "/", name, kImgSuffix);
    CF_EXPECT(FileExists(image_path));
    return image_path;
  }

 private:
  std::ostream& Format(std::ostream& out) const override {
    return out << "AndroidProductDir { .path_ = '" << path_ << "' }";
  }

  std::string path_;
};

}  // namespace

Result<std::unique_ptr<AndroidBuild>> AndroidProductDir(std::string path) {
  auto product_dir = std::make_unique<AndroidProductDirImpl>(std::move(path));

  std::set<std::string, std::less<void>> images =
      CF_EXPECT(product_dir->Images());
  CF_EXPECT(!images.empty());

  return product_dir;
}

}  // namespace cuttlefish
