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

namespace {

const std::string DEFAULT_BRANCH = "aosp-master";
const std::string DEFAULT_BUILD_TARGET = "aosp_cf_x86_phone-userdebug";

}

DEFINE_string(default_build, DEFAULT_BRANCH + "/" + DEFAULT_BUILD_TARGET,
              "source for the cuttlefish build to use (vendor.img + host)");
DEFINE_string(system_build, "", "source for system.img and product.img");
DEFINE_string(kernel_build, "", "source for the kernel or gki target");
DEFINE_string(otatools_build, "", "source for the host ota tools");

DEFINE_bool(download_img_zip, true, "Whether to fetch the -img-*.zip file.");
DEFINE_bool(download_target_files_zip, false, "Whether to fetch the "
                                              "-target_files-*.zip file.");

DEFINE_string(credential_source, "", "Build API credential source");
DEFINE_string(directory, cvd::CurrentDirectory(), "Target directory to fetch "
                                                  "files into");
DEFINE_bool(run_next_stage, false, "Continue running the device through the next stage.");
DEFINE_string(wait_retry_period, "20", "Retry period for pending builds given "
                                       "in seconds. Set to 0 to not wait.");

namespace {

const std::string HOST_TOOLS = "cvd-host_package.tar.gz";
const std::string OTA_TOOLS = "otatools.zip";

/** Returns the name of one of the artifact target zip files.
 *
 * For example, for a target "aosp_cf_x86_phone-userdebug" at a build "5824130",
 * the image zip file would be "aosp_cf_x86_phone-img-5824130.zip"
 */
std::string target_build_zip(const DeviceBuild& build, const std::string& name) {
  std::string target = build.target;
  size_t dash_pos = target.find("-");
  if (dash_pos != std::string::npos) {
    target.replace(dash_pos, target.size() - dash_pos, "");
  }
  return target + "-" + name + "-" + build.id + ".zip";
}

bool download_images(BuildApi* build_api, const DeviceBuild& build,
                     const std::string& target_directory,
                     const std::vector<std::string>& images) {
  std::string img_zip_name = target_build_zip(build, "img");
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
  if (!build_api->ArtifactToFile(build, img_zip_name, local_path)) {
    LOG(ERROR) << "Unable to download " << build << ":" << img_zip_name << " to "
        << local_path;
    return false;
  }

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

bool download_target_files(BuildApi* build_api, const DeviceBuild& build,
                           const std::string& target_directory) {
  std::string target_zip = target_build_zip(build, "target_files");
  auto artifacts = build_api->Artifacts(build);
  bool has_target_zip = false;
  for (const auto& artifact : artifacts) {
    has_target_zip |= artifact.Name() == target_zip;
  }
  if (!has_target_zip) {
    LOG(ERROR) << "Target " << build.target << " at id " << build.id
        << " did not have " << target_zip;
    return false;
  }
  std::string local_path = target_directory + "/" + target_zip;
  if (!build_api->ArtifactToFile(build, target_zip, local_path)) {
    LOG(ERROR) << "Unable to download " << build << ":" << target_zip << " to "
        << local_path;
    return false;
  }
  return true;
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

  if (!build_api->ArtifactToFile(build, HOST_TOOLS, local_path)) {
    LOG(ERROR) << "Unable to download " << build << ":" << HOST_TOOLS << " to "
        << local_path;
    return false;
  }

  cvd::Command tar_cmd("/bin/tar");
  tar_cmd.AddParameter("xvf");
  tar_cmd.AddParameter(local_path);
  tar_cmd.AddParameter("-C");
  tar_cmd.AddParameter(target_directory);
  tar_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdOut,
                        cvd::Subprocess::StdIOChannel::kStdErr);
  if (tar_cmd.Start().Wait() != 0) {
    LOG(FATAL) << "Could not extract " << local_path;
    return false;
  }
  if (unlink(local_path.c_str()) != 0) {
    LOG(ERROR) << "Could not delete " << local_path;
  }
  return true;
}

bool desparse(const std::string& file) {
  LOG(INFO) << "Unsparsing " << file;
  cvd::Command dd_cmd("/bin/dd");
  dd_cmd.AddParameter("if=", file);
  dd_cmd.AddParameter("of=", file);
  dd_cmd.AddParameter("conv=notrunc");
  dd_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdOut,
                       cvd::Subprocess::StdIOChannel::kStdErr);
  if (dd_cmd.Start().Wait() != 0) {
    LOG(ERROR) << "Could not unsparse " << file;
    return false;
  }
  return true;
}

bool download_ota_tools(BuildApi* build_api, const DeviceBuild& build,
                        const std::string& target_directory) {
  auto artifacts = build_api->Artifacts(build);
  bool has_host_package = false;
  for (const auto& artifact : artifacts) {
    has_host_package |= artifact.Name() == OTA_TOOLS;
  }
  if (!has_host_package) {
    LOG(ERROR) << "Target " << build.target << " at id " << build.id
        << " did not have " << OTA_TOOLS;
    return false;
  }
  std::string local_path = target_directory + "/" + OTA_TOOLS;

  if (!build_api->ArtifactToFile(build, OTA_TOOLS, local_path)) {
    LOG(ERROR) << "Unable to download " << build << ":" << OTA_TOOLS << " to "
        << local_path;
    return false;
  }

  std::string otatools_dir = target_directory + "/otatools";
  if (!cvd::DirectoryExists(otatools_dir) && mkdir(otatools_dir.c_str(), 0777) != 0) {
    LOG(FATAL) << "Could not create " << otatools_dir;
    return false;
  }
  cvd::Command bsdtar_cmd("/usr/bin/bsdtar");
  bsdtar_cmd.AddParameter("-x");
  bsdtar_cmd.AddParameter("-v");
  bsdtar_cmd.AddParameter("-C");
  bsdtar_cmd.AddParameter(otatools_dir);
  bsdtar_cmd.AddParameter("-f");
  bsdtar_cmd.AddParameter(local_path);
  bsdtar_cmd.AddParameter("-S");
  bsdtar_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdOut,
                           cvd::Subprocess::StdIOChannel::kStdErr);
  if (bsdtar_cmd.Start().Wait() != 0) {
    LOG(FATAL) << "Could not extract " << local_path;
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
  std::chrono::seconds retry_period(std::stoi(FLAGS_wait_retry_period));

  curl_global_init(CURL_GLOBAL_DEFAULT);
  {
    std::unique_ptr<CredentialSource> credential_source;
    if (FLAGS_credential_source == "gce") {
      credential_source = GceMetadataCredentialSource::make();
    } else if (FLAGS_credential_source != "") {
      credential_source = FixedCredentialSource::make(FLAGS_credential_source);
    }
    BuildApi build_api(std::move(credential_source));

    DeviceBuild default_build = ArgumentToBuild(&build_api, FLAGS_default_build,
                                                DEFAULT_BUILD_TARGET,
                                                retry_period);

    if (!download_host_package(&build_api, default_build, target_dir)) {
      LOG(FATAL) << "Could not download host package for " << default_build;
    }
    if (FLAGS_system_build != "" || FLAGS_kernel_build != "" || FLAGS_otatools_build != "") {
      DeviceBuild ota_build = default_build;
      if (FLAGS_otatools_build != "") {
        ota_build = ArgumentToBuild(&build_api, FLAGS_otatools_build,
                                    DEFAULT_BUILD_TARGET, retry_period);
      }
      if (!download_ota_tools(&build_api, ota_build, target_dir)) {
        LOG(FATAL) << "Could not download ota tools for " << ota_build;
      }
    }
    if (FLAGS_download_img_zip) {
      if (!download_images(&build_api, default_build, target_dir)) {
        LOG(FATAL) << "Could not download images for " << default_build;
      }
      desparse(target_dir + "/userdata.img");
    }
    if (FLAGS_download_target_files_zip) {
      if (!download_target_files(&build_api, default_build, target_dir)) {
        LOG(FATAL) << "Could not download target files for " << default_build;
      }
    }

    if (FLAGS_system_build != "") {
      DeviceBuild system_build = ArgumentToBuild(&build_api, FLAGS_system_build,
                                                 DEFAULT_BUILD_TARGET,
                                                 retry_period);
      if (FLAGS_download_img_zip) {
        if (!download_images(&build_api, system_build, target_dir,
                            {"system.img"})) {
          LOG(FATAL) << "Could not download system image for " << system_build;
        }
      }
      if (FLAGS_download_target_files_zip) {
        if (!download_target_files(&build_api, system_build, target_dir)) {
          LOG(FATAL) << "Could not download target files for " << system_build;
        }
      }
    }

    if (FLAGS_kernel_build != "") {
      DeviceBuild kernel_build = ArgumentToBuild(&build_api, FLAGS_kernel_build,
                                                 "kernel", retry_period);

      if (!build_api.ArtifactToFile(kernel_build, "bzImage", target_dir + "/kernel")) {
        LOG(FATAL) << "Could not download " << kernel_build << ":bzImage to "
            << target_dir + "/kernel";
      }
      std::vector<Artifact> kernel_artifacts = build_api.Artifacts(kernel_build);
      for (const auto& artifact : kernel_artifacts) {
        if (artifact.Name() != "initramfs.img") {
          continue;
        }
        bool downloaded = build_api.ArtifactToFile(
            kernel_build, "initramfs.img", target_dir + "/initramfs.img");
        if (!downloaded) {
          LOG(FATAL) << "Could not download " << kernel_build << ":initramfs.img to "
                     << target_dir + "/initramfs.img";
        }
      }
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
  cvd::Command stop_cmd("bin/stop_cvd");
  stop_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdOut,
                         cvd::Subprocess::StdIOChannel::kStdErr);
  stop_cmd.Start().Wait();

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
