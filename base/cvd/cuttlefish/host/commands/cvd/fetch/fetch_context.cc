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

#include "cuttlefish/host/commands/cvd/fetch/fetch_context.h"

#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include "absl/strings/match.h"
#include "absl/strings/strip.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/fetch/builds.h"
#include "cuttlefish/host/commands/cvd/fetch/de_android_sparse.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_tracer.h"
#include "cuttlefish/host/commands/cvd/fetch/target_directories.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/web/android_build_api.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/web/build_api_zip.h"
#include "cuttlefish/host/libs/web/build_zip_name.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/zip_file.h"

namespace cuttlefish {

static constexpr mode_t kRwxAllMode = S_IRWXU | S_IRWXG | S_IRWXO;

using android::base::Dirname;

FetchArtifact::FetchArtifact(FetchBuildContext& context,
                             std::string artifact_name)
    : fetch_build_context_(context), artifact_name_(artifact_name) {}

Result<void> FetchArtifact::Download() {
  CF_EXPECT(DownloadTo(artifact_name_));
  return {};
}

Result<void> FetchArtifact::DownloadTo(std::string local_path) {
  std::string new_path =
      fmt::format("{}/{}", fetch_build_context_.target_directory_, local_path);

  if (downloaded_path_.empty()) {
    std::string downloaded =
        CF_EXPECT(fetch_build_context_.fetch_context_.build_api_.DownloadFile(
            fetch_build_context_.build_, fetch_build_context_.target_directory_,
            artifact_name_));
    size_t size = FileSize(downloaded);
    std::string download_phase = fmt::format("Downloaded '{}'", artifact_name_);
    fetch_build_context_.trace_.CompletePhase(download_phase, size);
    CF_EXPECT(fetch_build_context_.DesparseFiles({artifact_name_}));

    CF_EXPECT(EnsureDirectoryExists(android::base::Dirname(new_path)));
    CF_EXPECT(RenameFile(downloaded, new_path));

    downloaded_path_ = new_path;
    if (absl::EndsWith(downloaded_path_, ".zip")) {
      zip_ = CF_EXPECT(ZipOpenRead(downloaded_path_));
    }
  } else {
    CF_EXPECT(Copy(downloaded_path_, new_path));
  }

  CF_EXPECT(fetch_build_context_.AddFileToConfig(downloaded_path_));

  return {};
}

Result<ReadableZip*> FetchArtifact::AsZip() {
  if (!zip_) {
    zip_ = CF_EXPECT(
        ::cuttlefish::OpenZip(fetch_build_context_.fetch_context_.build_api_,
                              fetch_build_context_.build_, artifact_name_));
  }
  return &*zip_;
}

Result<void> FetchArtifact::ExtractAll() {
  CF_EXPECT(ExtractAll(""));
  return {};
}

Result<void> FetchArtifact::ExtractAll(const std::string& local_path) {
  ReadableZip* zip = CF_EXPECT(AsZip());
  size_t entries = CF_EXPECT(zip->NumEntries());
  for (uint64_t i = 0; i < entries; i++) {
    std::string member_name = CF_EXPECT(zip->EntryName(i));
    CF_EXPECT(!absl::StartsWith(member_name, "."));
    CF_EXPECT(!absl::StartsWith(member_name, "/"));
    CF_EXPECT(!absl::StrContains(member_name, "/../"));
    std::string extract_path = fmt::format("{}/{}", local_path, member_name);
    CF_EXPECT(ExtractOneTo(member_name, extract_path));
  }
  return {};
}

Result<void> FetchArtifact::ExtractOne(const std::string& member_name) {
  CF_EXPECT(ExtractOneTo(member_name, member_name));
  return {};
}

Result<void> FetchArtifact::ExtractOneTo(const std::string& member_name,
                                         const std::string& local_path) {
  ReadableZip* zip = CF_EXPECT(AsZip());
  std::string extract_path =
      fmt::format("{}/{}", fetch_build_context_.target_directory_, local_path);

  if (std::string dir = android::base::Dirname(extract_path); !dir.empty()) {
    CF_EXPECT(EnsureDirectoryExists(dir, kRwxAllMode));
  }

  CF_EXPECT(ExtractFile(*zip, member_name, extract_path));

  CF_EXPECT(fetch_build_context_.AddFileToConfig(extract_path));

  std::string phase =
      fmt::format("Extracted '{}' from '{}'", member_name, artifact_name_);
  fetch_build_context_.trace_.CompletePhase(std::move(phase),
                                            FileSize(extract_path));

  fetch_build_context_.DesparseFiles({local_path});

  return {};
}

Result<void> FetchArtifact::DeleteLocalFile() {
  if (downloaded_path_.empty()) {
    return {};
  }
  CF_EXPECT(RemoveFile(downloaded_path_));
  std::string_view base_dir =
      fetch_build_context_.fetch_context_.target_directories_.root;
  std::string_view config_name = absl::StripPrefix(downloaded_path_, base_dir);
  config_name = absl::StripPrefix(config_name, "/");
  CF_EXPECT(
      fetch_build_context_.fetch_context_.fetcher_config_.RemoveFileFromConfig(
          std::string(config_name)));
  downloaded_path_ = "";
  return {};
}

FetchBuildContext::FetchBuildContext(FetchContext& fetch_context,
                                     ::cuttlefish::Build build,
                                     std::string_view target_directory,
                                     FileSource file_source,
                                     FetchTracer::Trace trace)
    : fetch_context_(fetch_context),
      build_(std::move(build)),
      target_directory_(target_directory),
      file_source_(file_source),
      trace_(trace) {}

const Build& FetchBuildContext::Build() const { return build_; }

std::string FetchBuildContext::GetBuildZipName(const std::string& name) const {
  return ::cuttlefish::GetBuildZipName(build_, name);
}

std::optional<std::string> FetchBuildContext::GetFilepath() const {
  return ::cuttlefish::GetFilepath(build_);
}

FetchArtifact FetchBuildContext::Artifact(std::string artifact_name) {
  return FetchArtifact(*this, std::move(artifact_name));
}

Result<void> FetchBuildContext::DesparseFiles(std::vector<std::string> files) {
  std::vector<std::string> full_paths;
  for (const std::string_view file : files) {
    full_paths.emplace_back(fmt::format("{}/{}", target_directory_, file));
  }

  CF_EXPECT(DeAndroidSparse2(full_paths));

  size_t size = 0;
  for (const std::string& file : full_paths) {
    size += FileSize(file);
  }
  std::string phase = fmt::format("Desparsed [{}]", fmt::join(files, ", "));
  trace_.CompletePhase(std::move(phase), size);
  return {};
}

Result<void> FetchBuildContext::AddFileToConfig(std::string file) {
  auto [build_id, build_target] = GetBuildIdAndTarget(build_);
  CF_EXPECT(fetch_context_.fetcher_config_.AddFilesToConfig(
      file_source_, build_id, build_target, {file},
      fetch_context_.target_directories_.root, true));
  return {};
}

std::ostream& operator<<(std::ostream& out, const FetchBuildContext& context) {
  return out << context.Build();
}

FetchContext::FetchContext(BuildApi& build_api,
                           const TargetDirectories& target_directories,
                           const Builds& builds, FetcherConfig& fetcher_config,
                           FetchTracer& tracer)
    : build_api_(build_api),
      target_directories_(target_directories),
      builds_(builds),
      fetcher_config_(fetcher_config),
      tracer_(tracer) {}

std::optional<FetchBuildContext> FetchContext::DefaultBuild() {
  if (builds_.default_build) {
    return FetchBuildContext(
        *this, *builds_.default_build, target_directories_.root,
        FileSource::DEFAULT_BUILD, tracer_.NewTrace("Default"));
  } else {
    return {};
  }
}

std::optional<FetchBuildContext> FetchContext::SystemBuild() {
  if (builds_.system) {
    return FetchBuildContext(*this, *builds_.system, target_directories_.root,
                             FileSource::SYSTEM_BUILD,
                             tracer_.NewTrace("System"));
  } else {
    return {};
  }
}

std::optional<FetchBuildContext> FetchContext::KernelBuild() {
  if (builds_.kernel) {
    return FetchBuildContext(*this, *builds_.kernel, target_directories_.root,
                             FileSource::KERNEL_BUILD,
                             tracer_.NewTrace("Kernel"));
  } else {
    return {};
  }
}

std::optional<FetchBuildContext> FetchContext::BootBuild() {
  if (builds_.boot) {
    return FetchBuildContext(*this, *builds_.boot, target_directories_.root,
                             FileSource::BOOT_BUILD, tracer_.NewTrace("Boot"));
  } else {
    return {};
  }
}

std::optional<FetchBuildContext> FetchContext::BootloaderBuild() {
  if (builds_.bootloader) {
    return FetchBuildContext(
        *this, *builds_.bootloader, target_directories_.root,
        FileSource::BOOTLOADER_BUILD, tracer_.NewTrace("Bootloader"));
  } else {
    return {};
  }
}

std::optional<FetchBuildContext> FetchContext::AndroidEfiLoaderBuild() {
  if (builds_.android_efi_loader) {
    return FetchBuildContext(*this, *builds_.android_efi_loader,
                             target_directories_.root,
                             FileSource::ANDROID_EFI_LOADER_BUILD,
                             tracer_.NewTrace("Android EFI Loader"));
  } else {
    return {};
  }
}

std::optional<FetchBuildContext> FetchContext::OtaToolsBuild() {
  if (builds_.otatools) {
    return FetchBuildContext(
        *this, *builds_.otatools, target_directories_.otatools,
        FileSource::DEFAULT_BUILD, tracer_.NewTrace("OTA Tools"));
  } else {
    return {};
  }
}

}  // namespace cuttlefish
