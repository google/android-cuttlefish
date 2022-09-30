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

#include "host/commands/cvd/fetch_cvd.h"

#include <sys/stat.h>
#include <unistd.h>

#include <curl/curl.h>

#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/archive.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/fetcher_config.h"
#include "host/libs/web/build_api.h"
#include "host/libs/web/credential_source.h"
#include "host/libs/web/install_zip.h"

namespace {

const std::string DEFAULT_BRANCH = "aosp-master";
const std::string DEFAULT_BUILD_TARGET = "aosp_cf_x86_64_phone-userdebug";
}

using cuttlefish::CurrentDirectory;

DEFINE_string(api_key, "", "API key for the Android Build API");
DEFINE_string(default_build, DEFAULT_BRANCH + "/" + DEFAULT_BUILD_TARGET,
              "source for the cuttlefish build to use (vendor.img + host)");
DEFINE_string(system_build, "", "source for system.img and product.img");
DEFINE_string(kernel_build, "", "source for the kernel or gki target");
DEFINE_string(boot_build, "", "source for the boot or gki target");
DEFINE_string(boot_artifact, "", "name of the boot image in boot_build");
DEFINE_string(bootloader_build, "", "source for the bootloader target");
DEFINE_string(otatools_build, "", "source for the host ota tools");

DEFINE_bool(download_img_zip, true, "Whether to fetch the -img-*.zip file.");
DEFINE_bool(download_target_files_zip, false, "Whether to fetch the "
                                              "-target_files-*.zip file.");

DEFINE_string(credential_source, "", "Build API credential source");
DEFINE_string(directory, CurrentDirectory(), "Target directory to fetch "
                                             "files into");
DEFINE_bool(run_next_stage, false, "Continue running the device through the next stage.");
DEFINE_string(wait_retry_period, "20", "Retry period for pending builds given "
                                       "in seconds. Set to 0 to not wait.");
DEFINE_bool(keep_downloaded_archives, false, "Keep downloaded zip/tar.");
#ifdef __BIONIC__
DEFINE_bool(external_dns_resolver, true,
            "Use an out-of-process mechanism to resolve DNS queries");
#else
DEFINE_bool(external_dns_resolver, false,
            "Use an out-of-process mechanism to resolve DNS queries");
#endif

namespace cuttlefish {
namespace {

const std::string HOST_TOOLS = "cvd-host_package.tar.gz";
const std::string OTA_TOOLS = "otatools.zip";
const std::string OTA_TOOLS_DIR = "/otatools/";

static bool ArtifactsContains(const std::vector<Artifact>& artifacts,
                              const std::string& name) {
  for (const auto& artifact : artifacts) {
    if (artifact.Name() == name) {
      return true;
    }
  }
  return false;
}

/** Returns the name of one of the artifact target zip files.
 *
 * For example, for a target "aosp_cf_x86_phone-userdebug" at a build "5824130",
 * the image zip file would be "aosp_cf_x86_phone-img-5824130.zip"
 */
Result<std::string> TargetBuildZipFromArtifacts(
    const Build& build, const std::string& name,
    const std::vector<Artifact>& artifacts) {
  std::string product = std::visit([](auto&& arg) { return arg.product; }, build);
  auto id = std::visit([](auto&& arg) { return arg.id; }, build);
  auto match = product + "-" + name + "-" + id + ".zip";
  CF_EXPECT(ArtifactsContains(artifacts, match));
  return match;
}

Result<std::string> DownloadImageZip(BuildApi& build_api, const Build& build,
                                     const std::string& target_directory) {
  auto artifacts = CF_EXPECT(build_api.Artifacts(build));
  std::string img_zip_name =
      CF_EXPECT(TargetBuildZipFromArtifacts(build, "img", artifacts));
  std::string local_path = target_directory + "/" + img_zip_name;
  CF_EXPECT(build_api.ArtifactToFile(build, img_zip_name, local_path),
            "Unable to download " << build << ":" << img_zip_name << " to "
                                  << local_path);
  return local_path;
}

Result<std::vector<std::string>> DownloadImages(
    BuildApi& build_api, const Build& build,
    const std::string& target_directory,
    const std::vector<std::string>& images) {
  std::string local_path =
      CF_EXPECT(DownloadImageZip(build_api, build, target_directory));

  std::vector<std::string> files = ExtractImages(local_path, target_directory, images);
  CF_EXPECT(!files.empty(), "Could not extract " << local_path);
  if (!FLAGS_keep_downloaded_archives && unlink(local_path.c_str()) != 0) {
    LOG(ERROR) << "Could not delete " << local_path;
    files.push_back(local_path);
  }
  return files;
}

Result<std::vector<std::string>> DownloadImages(
    BuildApi& build_api, const Build& build,
    const std::string& target_directory) {
  return DownloadImages(build_api, build, target_directory, {});
}

Result<std::vector<std::string>> DownloadTargetFiles(
    BuildApi& build_api, const Build& build,
    const std::string& target_directory) {
  auto artifacts = CF_EXPECT(build_api.Artifacts(build));
  std::string target_zip =
      CF_EXPECT(TargetBuildZipFromArtifacts(build, "target_files", artifacts));
  std::string local_path = target_directory + "/" + target_zip;
  CF_EXPECT(build_api.ArtifactToFile(build, target_zip, local_path),
            "Unable to download " << build << ":" << target_zip << " to "
                                  << local_path);
  return {{local_path}};
}

Result<std::vector<std::string>> DownloadHostPackage(
    BuildApi& build_api, const Build& build,
    const std::string& target_directory) {
  auto artifacts = CF_EXPECT(build_api.Artifacts(build));
  CF_EXPECT(ArtifactsContains(artifacts, HOST_TOOLS),
            "Target " << build << " did not have \"" << HOST_TOOLS << "\"");
  std::string local_path = target_directory + "/" + HOST_TOOLS;

  CF_EXPECT(build_api.ArtifactToFile(build, HOST_TOOLS, local_path),
            "Unable to download " << build << ":" << HOST_TOOLS << " to "
                                  << local_path);

  Archive archive(local_path);
  CF_EXPECT(archive.ExtractAll(target_directory),
            "Could not extract \"" << local_path << "\" to \""
                                   << target_directory << "\"");
  std::vector<std::string> files = archive.Contents();
  for (auto& file : files) {
    file = target_directory + "/" + file;
  }
  if (!FLAGS_keep_downloaded_archives && unlink(local_path.c_str()) != 0) {
    LOG(ERROR) << "Could not delete " << local_path;
    files.push_back(local_path);
  }
  return files;
}

Result<std::vector<std::string>> DownloadOtaTools(
    BuildApi& build_api, const Build& build,
    const std::string& target_directory) {
  auto artifacts = CF_EXPECT(build_api.Artifacts(build));
  CF_EXPECT(ArtifactsContains(artifacts, OTA_TOOLS),
            "Target " << build << " did not have " << OTA_TOOLS);
  std::string local_path = target_directory + "/" + OTA_TOOLS;

  CF_EXPECT(build_api.ArtifactToFile(build, OTA_TOOLS, local_path),
            "Unable to download " << build << ":" << OTA_TOOLS << " to "
                                  << local_path);

  std::string otatools_dir = target_directory + OTA_TOOLS_DIR;
  if (!DirectoryExists(otatools_dir)) {
    CF_EXPECT(
        mkdir(otatools_dir.c_str(), 0777) == 0,
        "Could not create \"" << otatools_dir << "\": " << strerror(errno));
  }
  Archive archive(local_path);
  CF_EXPECT(archive.ExtractAll(otatools_dir), "Failed to extract \""
                                                  << local_path << "\" to \""
                                                  << otatools_dir << "\"");
  std::vector<std::string> files = archive.Contents();
  for (auto& file : files) {
    file = target_directory + OTA_TOOLS_DIR + file;
  }
  files.push_back(local_path);
  return files;
}

Result<std::vector<std::string>> DownloadBoot(
    BuildApi& build_api, Build& build, const std::string& specified_artifact,
    const std::string& target_dir) {
  std::string target_boot = target_dir + "/boot.img";
  const std::string& boot_artifact =
      specified_artifact != "" ? specified_artifact : "boot.img";
  if (specified_artifact != "") {
    auto artifacts = CF_EXPECT(build_api.Artifacts(build));
    if (ArtifactsContains(artifacts, boot_artifact)) {
      CF_EXPECT(build_api.ArtifactToFile(build, boot_artifact, target_boot),
                "Could not download " << build << ":" << boot_artifact << " to "
                                      << target_boot);
      return {{target_boot}};
    }
    LOG(INFO) << "Find " << boot_artifact << " in the img zip";
  }

  std::vector<std::string> files{target_boot};
  std::string img_zip =
      CF_EXPECT(DownloadImageZip(build_api, build, target_dir));
  std::vector<std::string> extracted_boot =
      ExtractImages(img_zip, target_dir, {boot_artifact});
  CF_EXPECT(!extracted_boot.empty(),
            "No " << boot_artifact << " in the img zip.");
  if (extracted_boot[0] != target_boot) {
    CF_EXPECT(rename(extracted_boot[0].c_str(), target_boot.c_str()) == 0,
              "rename(\"" << extracted_boot[0] << "\", \"" << target_boot
                          << "\") failed: " << strerror(errno));
  }

  std::vector<std::string> extracted_vendor_boot =
      ExtractImages(img_zip, target_dir, {"vendor_boot.img"});
  if (!extracted_vendor_boot.empty()) {
    files.push_back(extracted_vendor_boot[0]);
  } else {
    LOG(INFO) << "No vendor_boot.img in the img zip.";
  }

  if (!FLAGS_keep_downloaded_archives && unlink(img_zip.c_str()) != 0) {
    LOG(ERROR) << "Could not delete " << img_zip;
  }
  return files;
}

Result<void> AddFilesToConfig(FileSource purpose, const Build& build,
                              const std::vector<std::string>& paths,
                              FetcherConfig* config,
                              const std::string& directory_prefix,
                              bool override_entry = false) {
  for (const std::string& path : paths) {
    std::string_view local_path(path);
    if (!android::base::ConsumePrefix(&local_path, directory_prefix)) {
      LOG(ERROR) << "Failed to remove prefix " << directory_prefix << " from "
                 << local_path;
    }
    while (android::base::StartsWith(local_path, "/")) {
      android::base::ConsumePrefix(&local_path, "/");
    }
    // TODO(schuffelen): Do better for local builds here.
    auto id = std::visit([](auto&& arg) { return arg.id; }, build);
    auto target = std::visit([](auto&& arg) { return arg.target; }, build);
    CvdFile file(purpose, id, target, std::string(local_path));
    CF_EXPECT(config->add_cvd_file(file, override_entry),
              "Duplicate file \"" << file << "\", Existing file: \""
                                  << config->get_cvd_files()[path]
                                  << "\". Failed to add path \"" << path
                                  << "\"");
  }
  return {};
}

std::string USAGE_MESSAGE =
    "<flags>\n"
    "\n"
    "\"*_build\" flags accept values in the following format:\n"
    "\"branch/build_target\" - latest build of \"branch\" for \"build_target\"\n"
    "\"build_id/build_target\" - build \"build_id\" for \"build_target\"\n"
    "\"branch\" - latest build of \"branch\" for \"aosp_cf_x86_phone-userdebug\"\n"
    "\"build_id\" - build \"build_id\" for \"aosp_cf_x86_phone-userdebug\"\n";

std::unique_ptr<CredentialSource> TryOpenServiceAccountFile(
    HttpClient& http_client, const std::string& path) {
  LOG(VERBOSE) << "Attempting to open service account file \"" << path << "\"";
  Json::CharReaderBuilder builder;
  std::ifstream ifs(path);
  Json::Value content;
  std::string errorMessage;
  if (!Json::parseFromStream(builder, ifs, &content, &errorMessage)) {
    LOG(VERBOSE) << "Could not read config file \"" << path
                 << "\": " << errorMessage;
    return {};
  }
  static constexpr char BUILD_SCOPE[] =
      "https://www.googleapis.com/auth/androidbuild.internal";
  auto result = ServiceAccountOauthCredentialSource::FromJson(
      http_client, content, BUILD_SCOPE);
  if (!result.ok()) {
    LOG(VERBOSE) << "Failed to load service account json file: \n"
                 << result.error().Trace();
    return {};
  }
  return std::unique_ptr<CredentialSource>(
      new ServiceAccountOauthCredentialSource(std::move(*result)));
}

Result<void> ProcessHostPackage(BuildApi& build_api, const Build& default_build,
                                const std::string& target_dir,
                                FetcherConfig* config) {
  std::vector<std::string> host_package_files =
      CF_EXPECT(DownloadHostPackage(build_api, default_build, target_dir));
  CF_EXPECT(!host_package_files.empty(),
            "Could not download host package for " << default_build);
  CF_EXPECT(AddFilesToConfig(FileSource::DEFAULT_BUILD, default_build,
                             host_package_files, config, target_dir));
  return {};
}

} // namespace

Result<void> FetchCvdMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  gflags::SetUsageMessage(USAGE_MESSAGE);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  FetcherConfig config;
  config.RecordFlags();

#ifdef __BIONIC__
  // TODO(schuffelen): Find a better way to deal with tzdata
  setenv("ANDROID_TZDATA_ROOT", "/", /* overwrite */ 0);
  setenv("ANDROID_ROOT", "/", /* overwrite */ 0);
#endif

  std::string target_dir = AbsolutePath(FLAGS_directory);
  if (!DirectoryExists(target_dir)) {
    CF_EXPECT(mkdir(target_dir.c_str(), 0777) == 0,
              "mkdir(" << target_dir << ", 0777) failed: " << strerror(errno));
  }
  std::string target_dir_slash = target_dir;
  std::chrono::seconds retry_period(std::stoi(FLAGS_wait_retry_period));

  curl_global_init(CURL_GLOBAL_DEFAULT);
  {
    auto resolver =
        FLAGS_external_dns_resolver ? GetEntDnsResolve : NameResolver();
    auto curl = HttpClient::CurlClient(resolver);
    auto retrying_http_client = HttpClient::ServerErrorRetryClient(
        *curl, 10, std::chrono::milliseconds(5000));
    std::unique_ptr<CredentialSource> credential_source;
    if (auto crds = TryOpenServiceAccountFile(*curl, FLAGS_credential_source)) {
      credential_source = std::move(crds);
    } else if (FLAGS_credential_source == "gce") {
      credential_source =
          GceMetadataCredentialSource::make(*retrying_http_client);
    } else if (FLAGS_credential_source == "") {
      std::string file = StringFromEnv("HOME", ".") + "/.acloud_oauth2.dat";
      LOG(VERBOSE) << "Probing acloud credentials at " << file;
      if (FileExists(file)) {
        std::ifstream stream(file);
        auto attempt_load =
            RefreshCredentialSource::FromOauth2ClientFile(*curl, stream);
        if (attempt_load.ok()) {
          credential_source.reset(
              new RefreshCredentialSource(std::move(*attempt_load)));
        } else {
          LOG(VERBOSE) << "Failed to load acloud credentials: "
                       << attempt_load.error().Trace();
        }
      } else {
        LOG(INFO) << "\"" << file << "\" missing, running without credentials";
      }
    } else {
      credential_source = FixedCredentialSource::make(FLAGS_credential_source);
    }
    BuildApi build_api(*retrying_http_client, credential_source.get(),
                       FLAGS_api_key);

    auto default_build = CF_EXPECT(ArgumentToBuild(
        build_api, FLAGS_default_build, DEFAULT_BUILD_TARGET, retry_period));

    auto process_pkg_ret =
        std::async(std::launch::async, ProcessHostPackage, std::ref(build_api),
                   std::cref(default_build), std::cref(target_dir), &config);

    if (FLAGS_system_build != "" || FLAGS_kernel_build != "" || FLAGS_otatools_build != "") {
      auto ota_build = default_build;
      if (FLAGS_otatools_build != "") {
        ota_build =
            CF_EXPECT(ArgumentToBuild(build_api, FLAGS_otatools_build,
                                      DEFAULT_BUILD_TARGET, retry_period));
      } else if (FLAGS_system_build != "") {
        ota_build = CF_EXPECT(ArgumentToBuild(
            build_api, FLAGS_system_build, DEFAULT_BUILD_TARGET, retry_period));
      }
      std::vector<std::string> ota_tools_files =
          CF_EXPECT(DownloadOtaTools(build_api, ota_build, target_dir));
      CF_EXPECT(!ota_tools_files.empty(),
                "Could not download ota tools for " << ota_build);
      CF_EXPECT(AddFilesToConfig(FileSource::DEFAULT_BUILD, default_build,
                                 ota_tools_files, &config, target_dir));
    }
    if (FLAGS_download_img_zip) {
      std::vector<std::string> image_files =
          CF_EXPECT(DownloadImages(build_api, default_build, target_dir));
      CF_EXPECT(!image_files.empty(),
                "Could not download images for " << default_build);
      LOG(INFO) << "Adding img-zip files for default build";
      for (auto& file : image_files) {
        LOG(INFO) << file;
      }
      CF_EXPECT(AddFilesToConfig(FileSource::DEFAULT_BUILD, default_build,
                                 image_files, &config, target_dir));
    }
    if (FLAGS_system_build != "" || FLAGS_download_target_files_zip) {
      std::string default_target_dir = target_dir + "/default";
      CF_EXPECT(mkdir(default_target_dir.c_str(),
                      S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0,
                "Could not create " << default_target_dir);
      std::vector<std::string> target_files = CF_EXPECT(
          DownloadTargetFiles(build_api, default_build, default_target_dir));
      CF_EXPECT(!target_files.empty(),
                "Could not download target files for " << default_build);
      LOG(INFO) << "Adding target files for default build";
      CF_EXPECT(AddFilesToConfig(FileSource::DEFAULT_BUILD, default_build,
                                 target_files, &config, target_dir));
    }

    if (FLAGS_system_build != "") {
      auto system_build = CF_EXPECT(ArgumentToBuild(
          build_api, FLAGS_system_build, DEFAULT_BUILD_TARGET, retry_period));
      bool system_in_img_zip = true;
      if (FLAGS_download_img_zip) {
        auto image_files = DownloadImages(build_api, system_build, target_dir,
                                          {"system.img", "product.img"});
        if (!image_files.ok() || image_files->empty()) {
          LOG(INFO) << "Could not find system image for " << system_build
                    << "in the img zip. Assuming a super image build, which will "
                    << "get the system image from the target zip.";
          system_in_img_zip = false;
        } else {
          LOG(INFO) << "Adding img-zip files for system build";
          CF_EXPECT(AddFilesToConfig(FileSource::SYSTEM_BUILD, system_build,
                                     *image_files, &config, target_dir, true));
        }
      }
      std::string system_target_dir = target_dir + "/system";
      CF_EXPECT(mkdir(system_target_dir.c_str(),
                      S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0,
                "Could not create \"" << system_target_dir
                                      << "\": " << strerror(errno));
      std::vector<std::string> target_files = CF_EXPECT(
          DownloadTargetFiles(build_api, system_build, system_target_dir));
      CF_EXPECT(!target_files.empty(),
                "Could not download target files for " << system_build);
      CF_EXPECT(AddFilesToConfig(FileSource::SYSTEM_BUILD, system_build,
                                 target_files, &config, target_dir));
      if (!system_in_img_zip) {
        if (ExtractImages(target_files[0], target_dir, {"IMAGES/system.img"})
            != std::vector<std::string>{}) {
          std::string extracted_system = target_dir + "/IMAGES/system.img";
          std::string target_system = target_dir + "/system.img";
          CF_EXPECT(
              rename(extracted_system.c_str(), target_system.c_str()) == 0,
              "rename(\"" << extracted_system << "\", \"" << target_system
                          << "\") failed: " << strerror(errno));
        } else {
          return CF_ERR("Could not get system.img from the target zip");
        }
        if (ExtractImages(target_files[0], target_dir,
                          {"IMAGES/product.img"}) !=
            std::vector<std::string>{}) {
          std::string extracted_product = target_dir + "/IMAGES/product.img";
          std::string target_product = target_dir + "/product.img";
          CF_EXPECT(
              rename(extracted_product.c_str(), target_product.c_str()) == 0,
              "rename(\"" << extracted_product << "\", \"" << target_product
                          << "\") failed: " << strerror(errno));
        }
        if (ExtractImages(target_files[0], target_dir, {"IMAGES/system_ext.img"})
            != std::vector<std::string>{}) {
          std::string extracted_system_ext = target_dir + "/IMAGES/system_ext.img";
          std::string target_system_ext = target_dir + "/system_ext.img";
          CF_EXPECT(rename(extracted_system_ext.c_str(),
                           target_system_ext.c_str()) == 0,
                    "rename(\"" << extracted_system_ext << "\", \""
                                << target_system_ext
                                << "\") failed: " << strerror(errno));
        }
        if (ExtractImages(target_files[0], target_dir, {"IMAGES/vbmeta_system.img"})
            != std::vector<std::string>{}) {
          std::string extracted_vbmeta_system = target_dir + "/IMAGES/vbmeta_system.img";
          std::string target_vbmeta_system = target_dir + "/vbmeta_system.img";
          CF_EXPECT(rename(extracted_vbmeta_system.c_str(),
                           target_vbmeta_system.c_str()) == 0,
                    "rename(\"" << extracted_vbmeta_system << "\", \""
                                << "\"" << target_vbmeta_system
                                << "\") failed: \"" << strerror(errno) << "\"");
        }
        // This should technically call AddFilesToConfig with the produced files,
        // but it will conflict with the ones produced from the default system image
        // and pie doesn't care about the produced file list anyway.
      }
    }

    if (FLAGS_kernel_build != "") {
      auto kernel_build = CF_EXPECT(ArgumentToBuild(
          build_api, FLAGS_kernel_build, "kernel", retry_period));

      std::string local_path = target_dir + "/kernel";
      if (!build_api.ArtifactToFile(kernel_build, "bzImage", local_path).ok()) {
        // If the kernel is from an arm/aarch64 build, the artifact will be
        // called Image.
        CF_EXPECT(build_api.ArtifactToFile(kernel_build, "Image", local_path),
                  "Could not download " << kernel_build << ":bzImage to "
                                        << local_path);
      }
      CF_EXPECT(AddFilesToConfig(FileSource::KERNEL_BUILD, kernel_build,
                                 {local_path}, &config, target_dir));
      auto kernel_artifacts = CF_EXPECT(build_api.Artifacts(kernel_build));
      for (const auto& artifact : kernel_artifacts) {
        if (artifact.Name() != "initramfs.img") {
          continue;
        }
        CF_EXPECT(build_api.ArtifactToFile(kernel_build, "initramfs.img",
                                           target_dir + "/initramfs.img"),
                  "Could not download " << kernel_build << ":initramfs.img to "
                                        << target_dir + "/initramfs.img");
        CF_EXPECT(AddFilesToConfig(FileSource::KERNEL_BUILD, kernel_build,
                                   {target_dir + "/initramfs.img"}, &config,
                                   target_dir));
      }
    }

    if (FLAGS_boot_build != "") {
      auto boot_build = CF_EXPECT(ArgumentToBuild(
          build_api, FLAGS_boot_build, "gki_x86_64-user", retry_period));
      std::vector<std::string> boot_files = CF_EXPECT(
          DownloadBoot(build_api, boot_build, FLAGS_boot_artifact, target_dir));
      CF_EXPECT(AddFilesToConfig(FileSource::BOOT_BUILD, boot_build, boot_files,
                                 &config, target_dir, true));
    }

    if (FLAGS_bootloader_build != "") {
      auto bootloader_build =
          CF_EXPECT(ArgumentToBuild(build_api, FLAGS_bootloader_build,
                                    "u-boot_crosvm_x86_64", retry_period));

      std::string local_path = target_dir + "/bootloader";
      if (!build_api.ArtifactToFile(bootloader_build, "u-boot.rom", local_path)
               .ok()) {
        // If the bootloader is from an arm/aarch64 build, the artifact will be
        // of filetype bin.
        CF_EXPECT(build_api.ArtifactToFile(bootloader_build, "u-boot.bin",
                                           local_path),
                  "Could not download " << bootloader_build << ":u-boot.rom to "
                                        << local_path);
      }
      CF_EXPECT(AddFilesToConfig(FileSource::BOOTLOADER_BUILD, bootloader_build,
                                 {local_path}, &config, target_dir, true));
    }

    // Wait for ProcessHostPackage to return.
    CF_EXPECT(process_pkg_ret.get(),
              "Could not download host package for " << default_build);
  }
  curl_global_cleanup();

  // Due to constraints of the build system, artifacts intentionally cannot determine
  // their own build id. So it's unclear which build number fetch_cvd itself was built at.
  // https://android.googlesource.com/platform/build/+/979c9f3/Changes.md#build_number
  std::string fetcher_path = target_dir + "/fetcher_config.json";
  CF_EXPECT(AddFilesToConfig(GENERATED, DeviceBuild("", ""), {fetcher_path},
                             &config, target_dir));
  config.SaveToFile(fetcher_path);

  for (const auto& file : config.get_cvd_files()) {
    std::cout << target_dir << "/" << file.second.file_path << "\n";
  }
  std::cout << std::flush;

  if (!FLAGS_run_next_stage) {
    return {};
  }

  // Ignore return code. We want to make sure there is no running instance,
  // and stop_cvd will exit with an error code if there is already no running instance.
  Command stop_cmd(target_dir + "/bin/stop_cvd");
  stop_cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut,
                         Subprocess::StdIOChannel::kStdErr);
  stop_cmd.Start().Wait();

  // gflags::ParseCommandLineFlags will remove fetch_cvd's flags from this.
  // This depends the remove_flags argument (3rd) is "true".

  auto filelist_fd = SharedFD::MemfdCreate("files_list");
  CF_EXPECT(filelist_fd->IsOpen(),
            "MemfdCreate failed: " << filelist_fd->StrError());

  for (const auto& file : config.get_cvd_files()) {
    std::string file_entry = file.second.file_path + "\n";
    auto chars_written =
        filelist_fd->Write(file_entry.c_str(), file_entry.size());
    if (chars_written != file_entry.size()) {
      return CF_ERR("Unable to write entry to file list. Expected to write "
                    << file_entry.size() << " but wrote " << chars_written
                    << ". " << filelist_fd->StrError() << " ("
                    << filelist_fd->GetErrno() << ")");
    }
  }
  CF_EXPECT(filelist_fd->LSeek(0, SEEK_SET) == 0, filelist_fd->StrError());

  CF_EXPECT(filelist_fd->UNMANAGED_Dup2(0) == 0,
            "Unable to set file list to stdin. " << filelist_fd->StrError());

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
  return CF_ERR("execv returned " << errno << ":" << strerror(errno));
}

} // namespace cuttlefish
