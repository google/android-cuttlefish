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

#include "gflags/gflags.h"
#include <glog/logging.h>

#include "common/libs/utils/subprocess.h"

#include "build_api.h"
#include "credential_source.h"

namespace {

const std::string& HOST_TOOLS = "cvd-host_package.tar.gz";

} // namespace

// TODO(schuffelen): Mixed builds.
DEFINE_string(build_id, "latest", "Build ID for all artifacts");
DEFINE_string(branch, "aosp-master", "Branch when build_id=\"latest\"");
DEFINE_string(target, "aosp_cf_x86_phone-userdebug", "Build target");
DEFINE_string(credential_source, "", "Build API credential source");

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  curl_global_init(CURL_GLOBAL_DEFAULT);
  {
    std::unique_ptr<CredentialSource> credential_source;
    if (FLAGS_credential_source == "gce") {
      credential_source = GceMetadataCredentialSource::make();
    }
    BuildApi build_api(std::move(credential_source));
    std::string build_id = FLAGS_build_id;
    if (build_id == "latest") {
      build_id = build_api.LatestBuildId(FLAGS_branch, FLAGS_target);
    }

    auto artifacts = build_api.Artifacts(build_id, FLAGS_target, "latest");
    bool has_host_package = false;
    bool has_image_zip = false;
    const std::string img_zip_name = FLAGS_target + "-img-" + build_id + ".zip";
    for (const auto& artifact : artifacts) {
      has_host_package |= artifact.Name() == HOST_TOOLS;
      has_image_zip |= artifact.Name() == img_zip_name;
    }
    if (!has_host_package) {
      LOG(FATAL) << "Target build " << build_id << " did not have " << HOST_TOOLS;
    }
    if (!has_image_zip) {
      LOG(FATAL) << "Target build " << build_id << " did not have " << img_zip_name;
    }

    build_api.ArtifactToFile(build_id, FLAGS_target, "latest",
                             HOST_TOOLS, HOST_TOOLS);
    build_api.ArtifactToFile(build_id, FLAGS_target, "latest",
                             img_zip_name, img_zip_name);

    if (cvd::execute({"/bin/tar", "xvf", HOST_TOOLS}) != 0) {
      LOG(FATAL) << "Could not extract " << HOST_TOOLS;
    }
    if (cvd::execute({"/usr/bin/unzip", img_zip_name}) != 0) {
      LOG(FATAL) << "Could not unzip " << img_zip_name;
    }

    if (unlink(HOST_TOOLS.c_str()) != 0) {
      LOG(ERROR) << "Could not delete " << HOST_TOOLS;
    }
    if (unlink(img_zip_name.c_str()) != 0) {
      LOG(ERROR) << "Could not delete " << img_zip_name;
    }
  }
  curl_global_cleanup();
}
