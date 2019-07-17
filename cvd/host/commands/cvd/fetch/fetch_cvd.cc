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

#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"

#include "build_api.h"
#include "credential_source.h"
#include "install_zip.h"

DEFINE_string(default_build, "aosp-master/aosp_cf_x86_phone-userdebug",
              "source for the cuttlefish build to use (vendor.img + host)");
DEFINE_string(system_build, "", "source for system.img and product.img");
DEFINE_string(kernel_build, "", "source for the kernel or gki target");

DEFINE_string(credential_source, "", "Build API credential source");
DEFINE_string(system_image_build_target, "", "Alternate target for the system "
                                             "image");
DEFINE_string(system_image_build_id, "", "Alternate build for the system "
                                         "image");
DEFINE_string(directory, cvd::CurrentDirectory(), "Target directory to fetch "
                                                  "files into");
DEFINE_bool(run_next_stage, false, "Continue running the device through the next stage.");

namespace {

const std::string& HOST_TOOLS = "cvd-host_package.tar.gz";

std::string target_image_zip(const DeviceBuild& build) {
  std::string target = build.target;
  if (target.find("-userdebug") != std::string::npos) {
    target.replace(target.find("-userdebug"), sizeof("-userdebug"), "");
  }
  if (target.find("-eng") != std::string::npos) {
    target.replace(target.find("-eng"), sizeof("-eng"), "");
  }
  return target + "-img-" + build.id + ".zip";
}

bool download_images(BuildApi* build_api, const DeviceBuild& build,
                     const std::string& target_directory,
                     const std::vector<std::string>& images) {
  std::string img_zip_name = target_image_zip(build);
  auto artifacts = build_api->Artifacts(build);
  bool has_image_zip = false;
  for (const auto& artifact : artifacts) {
    has_image_zip |= artifact.Name() == img_zip_name;
  }
  if (!has_image_zip) {
    LOG(ERROR) << "Target " << build.target << " at id " << build.id
        << " did not have " << img_zip_name;
    return false;
  }
  std::string local_path = target_directory + "/" + img_zip_name;
  build_api->ArtifactToFile(build, img_zip_name, local_path);

  auto could_extract = ExtractImages(local_path, target_directory, images);
  if (!could_extract) {
    LOG(ERROR) << "Could not extract " << local_path;
    return false;
  }
  if (unlink(local_path.c_str()) != 0) {
    LOG(ERROR) << "Could not delete " << local_path;
  }
  return true;
}
bool download_images(BuildApi* build_api, const DeviceBuild& build,
                     const std::string& target_directory) {
  return download_images(build_api, build, target_directory, {});
}

bool download_host_package(BuildApi* build_api, const DeviceBuild& build,
                           const std::string& target_directory) {
  auto artifacts = build_api->Artifacts(build);
  bool has_host_package = false;
  for (const auto& artifact : artifacts) {
    has_host_package |= artifact.Name() == HOST_TOOLS;
  }
  if (!has_host_package) {
    LOG(ERROR) << "Target " << build.target << " at id " << build.id
        << " did not have " << HOST_TOOLS;
    return false;
  }
  std::string local_path = target_directory + "/" + HOST_TOOLS;
  build_api->ArtifactToFile(build, HOST_TOOLS, local_path);
  if (cvd::execute({"/bin/tar", "xvf", local_path, "-C", target_directory}) != 0) {
    LOG(FATAL) << "Could not extract " << local_path;
    return false;
  }
  if (unlink(HOST_TOOLS.c_str()) != 0) {
    LOG(ERROR) << "Could not delete " << local_path;
  }
  return true;
}

bool desparse(const std::string& file) {
  LOG(INFO) << "Unsparsing " << file;
  if (cvd::execute({"/bin/dd", "if=" + file, "of=" + file, "conv=notrunc"}) != 0) {
    LOG(ERROR) << "Could not unsparse " << file;
    return false;
  }
  return true;
}

std::string USAGE_MESSAGE =
    "<flags>\n"
    "\n"
    "\"*_build\" flags accept values in the following format:\n"
    "\"branch/build_target\" - latest build of \"branch\" for \"build_target\"\n"
    "\"build_id/build_target\" - build \"build_id\" for \"build_target\"\n"
    "\"branch\" - latest build of \"branch\" for \"aosp_cf_x86_phone-userdebug\"\n"
    "\"build_id\" - build \"build_id\" for \"aosp_cf_x86_phone-userdebug\"\n";

} // namespace

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  gflags::SetUsageMessage(USAGE_MESSAGE);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string target_dir = cvd::AbsolutePath(FLAGS_directory);
  if (!cvd::DirectoryExists(target_dir) && mkdir(target_dir.c_str(), 0777) != 0) {
    LOG(FATAL) << "Could not create " << target_dir;
  }

  curl_global_init(CURL_GLOBAL_DEFAULT);
  {
    std::unique_ptr<CredentialSource> credential_source;
    if (FLAGS_credential_source == "gce") {
      credential_source = GceMetadataCredentialSource::make();
    } else if (FLAGS_credential_source != "") {
      credential_source = FixedCredentialSource::make(FLAGS_credential_source);
    }
    BuildApi build_api(std::move(credential_source));

    DeviceBuild default_build = ArgumentToBuild(&build_api, FLAGS_default_build);

    if (!download_host_package(&build_api, default_build, target_dir)) {
      LOG(FATAL) << "Could not download host package with target "
          << default_build.target << " and build id " << default_build.id;
    }
    if (!download_images(&build_api, default_build, target_dir)) {
      LOG(FATAL) << "Could not download images with target "
          << default_build.target << " and build id " << default_build.id;
    }

    desparse(target_dir + "/userdata.img");

    if (FLAGS_system_build != "") {
      DeviceBuild system_build = ArgumentToBuild(&build_api, FLAGS_system_build);

      if (!download_images(&build_api, system_build, target_dir,
                           {"system.img"})) {
        LOG(FATAL) << "Could not download system image at target "
            << system_build.target << " and build id " << system_build.id;
      }
    }

    if (FLAGS_kernel_build != "") {
      DeviceBuild kernel_build = ArgumentToBuild(&build_api, FLAGS_kernel_build);

      build_api.ArtifactToFile(kernel_build, "bzImage", target_dir + "/kernel");
    }
  }
  curl_global_cleanup();

  if (!FLAGS_run_next_stage) {
    return 0;
  }

  if (chdir(target_dir.c_str()) != 0) {
    int error_num = errno;
    LOG(FATAL) << "Could not change directory to \"" << target_dir << "\"."
        << "errno was " << error_num << " \"" << strerror(error_num) << "\"";
  }

  // Ignore return code. We want to make sure there is no running instance,
  // and stop_cvd will exit with an error code if there is already no running instance.
  cvd::execute({"bin/stop_cvd"});

  // gflags::ParseCommandLineFlags will remove fetch_cvd's flags from this.
  // This depends the remove_flags argument (3rd) is "true".

  // TODO(b/139199114): Go into assemble_cvd when the interface is stable and implemented.

  std::string next_stage = "bin/launch_cvd";
  std::vector<const char*> next_stage_argv = {"launch_cvd"};
  LOG(INFO) << "Running " << next_stage;
  for (int i = 1; i < argc; i++) {
    LOG(INFO) << argv[i];
    next_stage_argv.push_back(argv[i]);
  }
  next_stage_argv.push_back(nullptr);
  execv(next_stage.c_str(), const_cast<char* const*>(next_stage_argv.data()));
  int error = errno;
  LOG(FATAL) << "execv returned with errno " << error << ":" << strerror(error);
}
