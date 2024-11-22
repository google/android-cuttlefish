/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common/libs/utils/archive.h"

#include <unistd.h>

#include <string>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"

namespace cuttlefish {
namespace {

Result<std::vector<std::string>> ExtractHelper(
    std::vector<std::string>& files, const std::string& archive_filepath,
    const std::string& target_directory, const bool keep_archive) {
  CF_EXPECT(!files.empty(), "No files extracted from " << archive_filepath);

  auto it = files.begin();
  while (it != files.end()) {
    if (it->empty() || android::base::EndsWith(*it, "/")) {
      it = files.erase(it);
    } else {
      *it = target_directory + "/" + *it;
      it++;
    }
  }

  if (!keep_archive && unlink(archive_filepath.data()) != 0) {
    LOG(ERROR) << "Could not delete " << archive_filepath;
    files.push_back(archive_filepath);
  }

  return {files};
}

Result<std::vector<std::string>> ExtractFiles(
    const std::string& archive, const std::vector<std::string>& to_extract,
    const std::string& target_directory) {
  Command bsdtar_cmd = Command("/usr/bin/bsdtar")
                           .AddParameter("-x")
                           .AddParameter("-v")
                           .AddParameter("-C")
                           .AddParameter(target_directory)
                           .AddParameter("-f")
                           .AddParameter(archive)
                           .AddParameter("-S");
  for (const auto& extract : to_extract) {
    bsdtar_cmd.AddParameter(extract);
  }
  bsdtar_cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut,
                           Subprocess::StdIOChannel::kStdErr);
  std::string bsdtar_output;
  int bsdtar_ret = RunWithManagedStdio(std::move(bsdtar_cmd), nullptr, nullptr,
                                       &bsdtar_output);
  LOG(DEBUG) << bsdtar_output;
  CF_EXPECTF(bsdtar_ret == 0, "bsdtar extraction failed on '{}', '''{}'''",
             archive, bsdtar_output);

  std::vector<std::string> outputs = android::base::Split(bsdtar_output, "\n");
  for (std::string& output : outputs) {
    std::string_view view = output;
    android::base::ConsumePrefix(&view, "x ");
    output = view;
  }

  return outputs;
}

Result<std::vector<std::string>> ExtractAll(
    const std::string& archive, const std::string& target_directory) {
  std::vector<std::string> out =
      CF_EXPECT(ExtractFiles(archive, {}, target_directory));
  return out;
}

}  // namespace

Result<std::vector<std::string>> ExtractImages(
    const std::string& archive_filepath, const std::string& target_directory,
    const std::vector<std::string>& images, const bool keep_archive) {
  CF_EXPECT(ExtractFiles(archive_filepath, images, target_directory),
            "Could not extract images from \"" << archive_filepath << "\" to \""
                                               << target_directory << "\"");

  std::vector<std::string> files = images;
  return ExtractHelper(files, archive_filepath, target_directory, keep_archive);
}

Result<std::string> ExtractImage(const std::string& archive_filepath,
                                 const std::string& target_directory,
                                 const std::string& image,
                                 const bool keep_archive) {
  std::vector<std::string> result = CF_EXPECT(
      ExtractImages(archive_filepath, target_directory, {image}, keep_archive));
  return {result.front()};
}

namespace {

Result<std::string> hardlinkRecursively(const std::string& src_dir,
                                        const std::string& dst_dir,
                                        std::vector<std::string>& files) {
  DIR* dir = opendir(src_dir.c_str());
  if (!dir) {
    return CF_ERRF("Failed to open directory: '{}'", src_dir);
  }

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    std::string entry_name = entry->d_name;
    if (entry_name == "." || entry_name == "..") {
      continue;
    }

    std::string src_path = src_dir + "/" + entry_name;
    std::string dst_path = dst_dir + "/" + entry_name;
    struct stat st;
    if (stat(src_path.c_str(), &st) == -1) {
      closedir(dir);
      CF_ERRF("Failed to stat: '{}'", src_path);
    }

    if (S_ISDIR(st.st_mode)) {
      if (mkdir(dst_path.c_str(), st.st_mode) != 0) {
        closedir(dir);
        CF_ERRF("Failed to create directory: '{}'", dst_path);
      }
      Result<std::string> result =
          hardlinkRecursively(src_path, dst_path, files);
      if (!result.ok()) {
        closedir(dir);
        return result;
      }
    } else if (S_ISREG(st.st_mode)) {
      Result<std::string> result = CreateHardLink(src_path, dst_path, true);
      if (!result.ok()) {
        closedir(dir);
        return result;
      }
      files.push_back(dst_path);
    }
  }
  closedir(dir);
  return "";
}

}  // namespace

Result<std::vector<std::string>> ExtractArchiveContents(
    const std::string& archive_filepath, const std::string& target_directory,
    const bool keep_archive) {
  struct stat path_stat;
  if (stat(archive_filepath.c_str(), &path_stat) == 0 &&
      S_ISDIR(path_stat.st_mode)) {
    std::vector<std::string> files;
    CF_EXPECTF(hardlinkRecursively(archive_filepath, target_directory, files),
               "Cound not recursively hard link files from '{}' to '{}'",
               archive_filepath, target_directory);
    if (!keep_archive) {
      RecursivelyRemoveDirectory(archive_filepath);
    }
    return files;
  }
  std::vector<std::string> files =
      CF_EXPECT(ExtractAll(archive_filepath, target_directory),
                "Could not extract \"" << archive_filepath << "\" to \""
                                       << target_directory << "\"");

  return ExtractHelper(files, archive_filepath, target_directory, keep_archive);
}

} // namespace cuttlefish
