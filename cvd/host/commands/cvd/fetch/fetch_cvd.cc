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

#include <string>

#include <sys/stat.h>
#include <unistd.h>

#include <glog/logging.h>

#include "build_api.h"

namespace {

const std::string& TARGET = "aosp_cf_x86_phone-userdebug";
const std::string& HOST_TOOLS = "cvd-host_package.tar.gz";

} // namespace

int main(int, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  curl_global_init(CURL_GLOBAL_DEFAULT);
  {
    BuildApi build_api;
    std::string build_id =
        build_api.LatestBuildId("aosp-master", TARGET);

    auto artifacts = build_api.Artifacts(build_id, TARGET, "latest");
    bool has_host_package = false;
    bool has_image_zip = false;
    const std::string img_zip_name = "aosp_cf_x86_phone-img-" + build_id + ".zip";
    for (const auto& artifact : artifacts) {
      has_host_package |= artifact.Name() == "cvd-host_package.tar.gz";
      has_image_zip |= artifact.Name() == img_zip_name;
    }
    if (!has_host_package) {
      LOG(FATAL) << "Target build " << build_id << " did not have cvd-host_package.tar.gz";
    }
    if (!has_image_zip) {
      LOG(FATAL) << "Target build " << build_id << " did not have" << img_zip_name;
    }

    build_api.ArtifactToFile(build_id, TARGET, "latest",
                             HOST_TOOLS, HOST_TOOLS);
    build_api.ArtifactToFile(build_id, TARGET, "latest",
                             img_zip_name,
                             "img.zip");
  }
  curl_global_cleanup();
}
