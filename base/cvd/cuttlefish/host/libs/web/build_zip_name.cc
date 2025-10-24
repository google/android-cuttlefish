//
// Copyright (C) 2019 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/build_zip_name.h"

#include <string>
#include <variant>

#include "third_party/android_cuttlefish/base/cvd/libbase/include/android-base/strings.h"
#include "cuttlefish/host/libs/web/android_build.h"

namespace cuttlefish {

std::string GetBuildZipName(const Build& build, const std::string& name) {
  std::string product =
      std::visit([](auto&& arg) { return arg.product; }, build);
  const auto is_signed =
      std::visit([](auto&& arg) { return arg.is_signed; }, build);
  const auto& target = std::visit(
      [](const auto& arg) -> const std::string& { return arg.target; }, build);
  const auto id = std::visit([](auto&& arg) { return arg.id; }, build);
  std::string dir_prefix;
  if (is_signed && cf_android::base::EndsWith(target, "-user") &&
      !cf_android::base::EndsWith(target, "-userdebug")) {
    dir_prefix = "signed/";
  }

  const auto filepath =
      std::visit([](auto&& arg) { return arg.filepath; }, build);
  if (filepath && !filepath->empty()) {
    return dir_prefix + *filepath;
  }

  return product + "-" + name + "-" + id + ".zip";
}

}  // namespace cuttlefish
