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

#include "cuttlefish/host/libs/web/directory_build_api.h"

#include <dirent.h>
#include <unistd.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "android-base/file.h"

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/writable_source.h"
#include "cuttlefish/posix/symlink.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

struct CloseDir {
  void operator()(DIR* dir) { closedir(dir); }
};

}  // namespace

Result<DirectoryBuild> DirectoryBuildApi::GetBuild(
    const DirectoryBuildString& build_string) {
  return DirectoryBuild(build_string.paths, build_string.target,
                        build_string.filepath);
}

Result<std::string> DirectoryBuildApi::DownloadFile(
    const DirectoryBuild& build, const std::string& target_directory,
    const std::string& artifact_name) {
  return CF_EXPECT(DownloadArtifact(build, target_directory, artifact_name));
}

Result<std::string> DirectoryBuildApi::DownloadArtifact(
    const DirectoryBuild& build, const std::string& target_directory,
    const std::string& artifact_name) {
  std::unordered_set<std::string> artifacts =
      CF_EXPECT(Artifacts(build, {artifact_name}));
  CF_EXPECT(Contains(artifacts, artifact_name),
            "Target " << build << " did not contain " << artifact_name);
  return DownloadTargetFile(build, target_directory, artifact_name);
}

Result<SeekableZipSource> DirectoryBuildApi::FileReader(
    const DirectoryBuild& build, const std::string& artifact_name) {
  for (const std::string& path : build.paths) {
    std::string zip_path_attempt = path + "/" + artifact_name;
    if (FileExists(zip_path_attempt)) {
      return CF_EXPECT(WritableZipSource::FromFile(zip_path_attempt));
    }
  }
  return CF_ERRF("Could not find '{}'", artifact_name);
}

Result<std::unordered_set<std::string>> DirectoryBuildApi::Artifacts(
    const DirectoryBuild& build, const std::vector<std::string>&) {
  std::unordered_set<std::string> artifacts;
  for (const auto& path : build.paths) {
    auto dir = std::unique_ptr<DIR, CloseDir>(opendir(path.c_str()));
    CF_EXPECT(dir != nullptr, "Could not read files from \"" << path << "\"");
    for (auto entity = readdir(dir.get()); entity != nullptr;
         entity = readdir(dir.get())) {
      artifacts.emplace(std::string(entity->d_name));
    }
  }
  return artifacts;
}

Result<void> DirectoryBuildApi::ArtifactToFile(const DirectoryBuild& build,
                                               const std::string& artifact,
                                               const std::string& path) {
  for (const auto& path : build.paths) {
    auto source = path + "/" + artifact;
    if (!FileExists(source)) {
      continue;
    }
    unlink(path.c_str());
    CF_EXPECT(Symlink(source, path));
    return {};
  }
  return CF_ERR("Could not find artifact \"" << artifact << "\" in build \""
                                             << build << "\"");
}

Result<std::string> DirectoryBuildApi::DownloadTargetFile(
    const DirectoryBuild& build, const std::string& target_directory,
    const std::string& artifact_name) {
  std::string target_filepath =
      ConstructTargetFilepath(target_directory, artifact_name);
  CF_EXPECT(EnsureDirectoryExists(
      std::string(android::base::Dirname(target_filepath))));
  CF_EXPECT(ArtifactToFile(build, artifact_name, target_filepath),
            "Unable to download " << build << ":" << artifact_name << " to "
                                  << target_filepath);
  return {target_filepath};
}

}  // namespace cuttlefish
