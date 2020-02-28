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

#include <iostream>
#include <iterator>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

#include "gflags/gflags.h"
#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/archive.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"

#include "host/libs/config/fetcher_config.h"

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
const std::string OTA_TOOLS_DIR = "/otatools/";

/** Returns the name of one of the artifact target zip files.
 *
 * For example, for a target "aosp_cf_x86_phone-userdebug" at a build "5824130",
 * the image zip file would be "aosp_cf_x86_phone-img-5824130.zip"
 */
std::string TargetBuildZipFromArtifacts(
    const Build& build, const std::string& name,
    const std::vector<Artifact>& artifacts) {
  std::string target = std::visit([](auto&& arg) { return arg.target; }, build);
  size_t dash_pos = target.find('-');
  if (dash_pos != std::string::npos) {
    target.replace(dash_pos, target.size() - dash_pos, "");
  }
  auto id = std::visit([](auto&& arg) { return arg.id; }, build);
  auto match = target + "-" + name + "-" + id;
  for (const auto& artifact : artifacts) {
    if (artifact.Name().find(match) != std::string::npos) {
      return artifact.Name();
    }
  }
  return "";
}

std::vector<std::string> download_images(BuildApi* build_api,
                                         const Build& build,
                                         const std::string& target_directory,
                                         const std::vector<std::string>& images) {
  auto artifacts = build_api->Artifacts(build);
  std::string img_zip_name = TargetBuildZipFromArtifacts(build, "img", artifacts);
  if (img_zip_name.size() == 0) {
    LOG(ERROR) << "Target " << build << " did not have an img zip";
    return {};
  }
  std::string local_path = target_directory + "/" + img_zip_name;
  if (!build_api->ArtifactToFile(build, img_zip_name, local_path)) {
    LOG(ERROR) << "Unable to download " << build << ":" << img_zip_name << " to "
               << local_path;
    return {};
  }

  std::vector<std::string> files = ExtractImages(local_path, target_directory, images);
  if (files.empty()) {
    LOG(ERROR) << "Could not extract " << local_path;
    return {};
  }
  if (unlink(local_path.c_str()) != 0) {
    LOG(ERROR) << "Could not delete " << local_path;
    files.push_back(local_path);
  }
  return files;
}
std::vector<std::string> download_images(BuildApi* build_api,
                                         const Build& build,
                                         const std::string& target_directory) {
  return download_images(build_api, build, target_directory, {});
}

std::vector<std::string> download_target_files(BuildApi* build_api,
                                               const Build& build,
                                               const std::string& target_directory) {
  auto artifacts = build_api->Artifacts(build);
  std::string target_zip = TargetBuildZipFromArtifacts(build, "target_files", artifacts);
  if (target_zip.size() == 0) {
    LOG(ERROR) << "Target " << build << " did not have a target files zip";
    return {};
  }
  std::string local_path = target_directory + "/" + target_zip;
  if (!build_api->ArtifactToFile(build, target_zip, local_path)) {
    LOG(ERROR) << "Unable to download " << build << ":" << target_zip << " to "
               << local_path;
    return {};
  }
  return {local_path};
}

std::vector<std::string> download_host_package(BuildApi* build_api,
                                               const Build& build,
                                               const std::string& target_directory) {
  auto artifacts = build_api->Artifacts(build);
  bool has_host_package = false;
  for (const auto& artifact : artifacts) {
    has_host_package |= artifact.Name() == HOST_TOOLS;
  }
  if (!has_host_package) {
    LOG(ERROR) << "Target " << build << " did not have " << HOST_TOOLS;
    return {};
  }
  std::string local_path = target_directory + "/" + HOST_TOOLS;

  if (!build_api->ArtifactToFile(build, HOST_TOOLS, local_path)) {
    LOG(ERROR) << "Unable to download " << build << ":" << HOST_TOOLS << " to "
               << local_path;
    return {};
  }

  cvd::Archive archive(local_path);
  if (!archive.ExtractAll(target_directory)) {
    LOG(ERROR) << "Could not extract " << local_path;
    return {};
  }
  std::vector<std::string> files = archive.Contents();
  for (auto& file : files) {
    file = target_directory + "/" + file;
  }
  if (unlink(local_path.c_str()) != 0) {
    LOG(ERROR) << "Could not delete " << local_path;
    files.push_back(local_path);
  }
  return files;
}

std::vector<std::string> download_ota_tools(BuildApi* build_api,
                                            const Build& build,
                                            const std::string& target_directory) {
  auto artifacts = build_api->Artifacts(build);
  bool has_host_package = false;
  for (const auto& artifact : artifacts) {
    has_host_package |= artifact.Name() == OTA_TOOLS;
  }
  if (!has_host_package) {
    LOG(ERROR) << "Target " << build << " did not have " << OTA_TOOLS;
    return {};
  }
  std::string local_path = target_directory + "/" + OTA_TOOLS;

  if (!build_api->ArtifactToFile(build, OTA_TOOLS, local_path)) {
    LOG(ERROR) << "Unable to download " << build << ":" << OTA_TOOLS << " to "
        << local_path;
    return {};
  }

  std::string otatools_dir = target_directory + OTA_TOOLS_DIR;
  if (!cvd::DirectoryExists(otatools_dir) && mkdir(otatools_dir.c_str(), 0777) != 0) {
    LOG(ERROR) << "Could not create " << otatools_dir;
    return {};
  }
  cvd::Archive archive(local_path);
  if (!archive.ExtractAll(otatools_dir)) {
    LOG(ERROR) << "Could not extract " << local_path;
    return {};
  }
  std::vector<std::string> files = archive.Contents();
  for (auto& file : files) {
    file = target_directory + OTA_TOOLS_DIR + file;
  }
  files.push_back(local_path);
  return files;
}

void AddFilesToConfig(cvd::FileSource purpose, const Build& build,
                      const std::vector<std::string>& paths, cvd::FetcherConfig* config,
                      bool override_entry = false) {
  for (const std::string& path : paths) {
    // TODO(schuffelen): Do better for local builds here.
    auto id = std::visit([](auto&& arg) { return arg.id; }, build);
    auto target = std::visit([](auto&& arg) { return arg.target; }, build);
    cvd::CvdFile file(purpose, id, target, path);
    bool added = config->add_cvd_file(file, override_entry);
    if (!added) {
      LOG(ERROR) << "Duplicate file " << file;
      LOG(ERROR) << "Existing file: " << config->get_cvd_files()[path];
      LOG(FATAL) << "Failed to add path " << path;
    }
  }
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

  cvd::FetcherConfig config;
  config.RecordFlags();

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

    auto default_build = ArgumentToBuild(&build_api, FLAGS_default_build,
                                         DEFAULT_BUILD_TARGET,
                                         retry_period);

    std::vector<std::string> host_package_files =
        download_host_package(&build_api, default_build, target_dir);
    if (host_package_files.empty()) {
      LOG(FATAL) << "Could not download host package for " << default_build;
    }
    AddFilesToConfig(cvd::FileSource::DEFAULT_BUILD, default_build, host_package_files, &config);

    if (FLAGS_system_build != "" || FLAGS_kernel_build != "" || FLAGS_otatools_build != "") {
      auto ota_build = default_build;
      if (FLAGS_otatools_build != "") {
        ota_build = ArgumentToBuild(&build_api, FLAGS_otatools_build,
                                    DEFAULT_BUILD_TARGET, retry_period);
      }
      std::vector<std::string> ota_tools_files =
          download_ota_tools(&build_api, ota_build, target_dir);
      if (ota_tools_files.empty()) {
        LOG(FATAL) << "Could not download ota tools for " << ota_build;
      }
      AddFilesToConfig(cvd::FileSource::DEFAULT_BUILD, default_build, ota_tools_files, &config);
    }
    if (FLAGS_download_img_zip) {
      std::vector<std::string> image_files =
          download_images(&build_api, default_build, target_dir);
      if (image_files.empty()) {
        LOG(FATAL) << "Could not download images for " << default_build;
      }
      LOG(INFO) << "Adding img-zip files for default build";
      for (auto& file : image_files) {
        LOG(INFO) << file;
      }
      AddFilesToConfig(cvd::FileSource::DEFAULT_BUILD, default_build, image_files, &config);
    }
    if (FLAGS_system_build != "" || FLAGS_download_target_files_zip) {
      std::string default_target_dir = target_dir + "/default";
      if (mkdir(default_target_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0) {
        LOG(FATAL) << "Could not create " << default_target_dir;
      }
      std::vector<std::string> target_files =
          download_target_files(&build_api, default_build, default_target_dir);
      if (target_files.empty()) {
        LOG(FATAL) << "Could not download target files for " << default_build;
      }
      LOG(INFO) << "Adding target files for default build";
      AddFilesToConfig(cvd::FileSource::DEFAULT_BUILD, default_build, target_files, &config);
    }

    if (FLAGS_system_build != "") {
      auto system_build = ArgumentToBuild(&build_api, FLAGS_system_build,
                                          DEFAULT_BUILD_TARGET,
                                          retry_period);
      bool system_in_img_zip = true;
      if (FLAGS_download_img_zip) {
        std::vector<std::string> image_files =
            download_images(&build_api, system_build, target_dir, {"system.img"});
        if (image_files.empty()) {
          LOG(INFO) << "Could not find system image for " << system_build
                    << "in the img zip. Assuming a super image build, which will "
                    << "get the super image from the target zip.";
          system_in_img_zip = false;
        } else {
          LOG(INFO) << "Adding img-zip files for system build";
          AddFilesToConfig(cvd::FileSource::SYSTEM_BUILD, system_build, image_files,
                           &config, true);
        }
      }
      std::string system_target_dir = target_dir + "/system";
      if (mkdir(system_target_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0) {
        LOG(FATAL) << "Could not create " << system_target_dir;
      }
      std::vector<std::string> target_files =
          download_target_files(&build_api, system_build, system_target_dir);
      if (target_files.empty()) {
        LOG(FATAL) << "Could not download target files for " << system_build;
        return -1;
      }
      AddFilesToConfig(cvd::FileSource::SYSTEM_BUILD, system_build, target_files, &config);
      if (!system_in_img_zip) {
        std::vector<std::string> wanted_images = {"IMAGES/system.img", "IMAGES/product.img"};
        auto images = ExtractImages(target_files[0], target_dir, wanted_images);
        if (images.size() != 2) {
          LOG(FATAL) << "Could not get system.img, product.img from target zip";
          return -1;
        }
        std::string extracted_system = target_dir + "/IMAGES/system.img";
        std::string target_system = target_dir + "/system.img";
        if (rename(extracted_system.c_str(), target_system.c_str())) {
          int error_num = errno;
          LOG(FATAL) << "Could not replace system.img in target directory: "
              << strerror(error_num);
          return -1;
        }
        std::string extracted_product = target_dir + "/IMAGES/product.img";
        std::string target_product = target_dir + "/product.img";
        if (rename(extracted_product.c_str(), target_product.c_str())) {
          int error_num = errno;
          LOG(FATAL) << "Could not replace product.img in target directory"
              << strerror(error_num);
          return -1;
        }
        // This should technically call AddFilesToConfig with the produced files,
        // but it will conflict with the ones produced from the default system image
        // and pie doesn't care about the produced file list anyway.
      }
    }

    if (FLAGS_kernel_build != "") {
      auto kernel_build = ArgumentToBuild(&build_api, FLAGS_kernel_build,
                                          "kernel", retry_period);

      std::string local_path = target_dir + "/kernel";
      if (build_api.ArtifactToFile(kernel_build, "bzImage", local_path)) {
        AddFilesToConfig(cvd::FileSource::KERNEL_BUILD, kernel_build, {local_path}, &config);
      } else {
        LOG(FATAL) << "Could not download " << kernel_build << ":bzImage to "
            << local_path;
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
        AddFilesToConfig(cvd::FileSource::KERNEL_BUILD, kernel_build,
                         {target_dir + "/initramfs.img"}, &config);
      }
    }
  }
  curl_global_cleanup();

  // Due to constraints of the build system, artifacts intentionally cannot determine
  // their own build id. So it's unclear which build number fetch_cvd itself was built at.
  // https://android.googlesource.com/platform/build/+/979c9f3/Changes.md#build_number
  std::string fetcher_path = target_dir + "/fetcher_config.json";
  AddFilesToConfig(cvd::GENERATED, DeviceBuild("", ""), {fetcher_path}, &config);
  config.SaveToFile(fetcher_path);

  for (const auto& file : config.get_cvd_files()) {
    std::cout << file.second.file_path << "\n";
  }
  std::cout << std::flush;

  if (!FLAGS_run_next_stage) {
    return 0;
  }

  // Ignore return code. We want to make sure there is no running instance,
  // and stop_cvd will exit with an error code if there is already no running instance.
  cvd::Command stop_cmd(target_dir + "/bin/stop_cvd");
  stop_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdOut,
                         cvd::Subprocess::StdIOChannel::kStdErr);
  stop_cmd.Start().Wait();

  // gflags::ParseCommandLineFlags will remove fetch_cvd's flags from this.
  // This depends the remove_flags argument (3rd) is "true".

  auto filelist_fd = cvd::SharedFD::MemfdCreate("files_list");
  if (!filelist_fd->IsOpen()) {
    LOG(FATAL) << "Unable to create temp file to write file list. "
               << filelist_fd->StrError() << " (" << filelist_fd->GetErrno() << ")";
  }

  for (const auto& file : config.get_cvd_files()) {
    std::string file_entry = file.second.file_path + "\n";
    auto chars_written = filelist_fd->Write(file_entry.c_str(), file_entry.size());
    if (chars_written != file_entry.size()) {
      LOG(FATAL) << "Unable to write entry to file list. Expected to write "
                 << file_entry.size() << " but wrote " << chars_written << ". "
                 << filelist_fd->StrError() << " (" << filelist_fd->GetErrno() << ")";
    }
  }
  auto seek_result = filelist_fd->LSeek(0, SEEK_SET);
  if (seek_result != 0) {
    LOG(FATAL) << "Unable to seek on file list file. Expected 0, received " << seek_result
               << filelist_fd->StrError() << " (" << filelist_fd->GetErrno() << ")";
  }

  if (filelist_fd->UNMANAGED_Dup2(0) == -1) {
    LOG(FATAL) << "Unable to set file list to stdin. "
               << filelist_fd->StrError() << " (" << filelist_fd->GetErrno() << ")";
  }

  // TODO(b/139199114): Go into assemble_cvd when the interface is stable and implemented.

  std::string next_stage = target_dir + "/bin/launch_cvd";
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
