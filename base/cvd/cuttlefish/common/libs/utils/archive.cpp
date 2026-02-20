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

#include "cuttlefish/common/libs/utils/archive.h"

#include <unistd.h>

#include <string>
#include <utility>
#include <vector>

#include <android-base/strings.h>
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<std::vector<std::string>> ExtractHelper(
    std::vector<std::string>& files, const std::string& archive_filepath,
    const std::string& target_directory, const bool keep_archive) {
  CF_EXPECT(!files.empty(), "No files extracted from " << archive_filepath);

  auto it = files.begin();
  while (it != files.end()) {
    if (it->empty() || absl::EndsWith(*it, "/")) {
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
  std::string bsdtar_stdout;
  std::string bsdtar_stderr;
  int exit_code = RunWithManagedStdio(std::move(bsdtar_cmd), nullptr,
                                      &bsdtar_stdout, &bsdtar_stderr);
  CF_EXPECTF(exit_code == 0,
             "Failed to execute 'bsdtar' <args>: exit code = {}, stdout = "
             "'{}', stderr = '{}'",
             exit_code, bsdtar_stdout, bsdtar_stderr);
  VLOG(0) << bsdtar_stderr;

  std::vector<std::string> outputs = absl::StrSplit(bsdtar_stderr, '\n');
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

Result<std::vector<std::string>> ExtractArchiveContents(
    const std::string& archive_filepath, const std::string& target_directory,
    const bool keep_archive) {
  std::vector<std::string> files =
      CF_EXPECT(ExtractAll(archive_filepath, target_directory),
                "Could not extract \"" << archive_filepath << "\" to \""
                                       << target_directory << "\"");

  return ExtractHelper(files, archive_filepath, target_directory, keep_archive);
}

std::string ExtractArchiveToMemory(const std::string& archive_filepath,
                                   const std::string& archive_member) {
  Command bsdtar_cmd("/usr/bin/bsdtar");
  bsdtar_cmd.AddParameter("-xf");
  bsdtar_cmd.AddParameter(archive_filepath);
  bsdtar_cmd.AddParameter("-O");
  bsdtar_cmd.AddParameter(archive_member);
  Result<std::string> stdout_str = RunAndCaptureStdout(std::move(bsdtar_cmd));

  if (!stdout_str.ok()) {
    LOG(ERROR) << "Could not extract \"" << archive_member << "\" from \""
               << archive_filepath << "\" to memory: " << stdout_str.error();
    return "";
  }
  return *stdout_str;
}

std::vector<std::string> ArchiveContents(const std::string& archive) {
  Command bsdtar_cmd("/usr/bin/bsdtar");
  bsdtar_cmd.AddParameter("-tf");
  bsdtar_cmd.AddParameter(archive);

  Result<std::string> bsdtar_output =
      RunAndCaptureStdout(std::move(bsdtar_cmd));
  if (bsdtar_output.ok()) {
    return absl::StrSplit(*bsdtar_output, '\n');
  } else {
    LOG(ERROR) << "`bsdtar -tf '" << archive
               << "'`failed: " << bsdtar_output.error();
    return {};
  }
}

} // namespace cuttlefish
