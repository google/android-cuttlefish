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

#include "install_zip.h"

#include <stdlib.h>

#include <string>
#include <vector>

#include <glog/logging.h>

#include "common/libs/utils/archive.h"
#include "common/libs/utils/subprocess.h"

std::vector<std::string> ExtractImages(const std::string& archive_file,
                                       const std::string& target_directory,
                                       const std::vector<std::string>& images) {
  cvd::Archive archive(archive_file);
  bool extracted =
      images.size() > 0
          ? archive.ExtractFiles(images, target_directory)
          : archive.ExtractAll(target_directory);
  if (!extracted) {
    LOG(ERROR) << "Unable to extract images.";
    return {};
  }

  bool extraction_success = true;
  std::vector<std::string> files =
      images.size() > 0 ? images : archive.Contents();
  for (const auto& file : files) {
    if (file.find(".img") == std::string::npos) {
      continue;
    }
    std::string extracted_file = target_directory + "/" + file;

    std::string file_output;
    auto file_ret = cvd::execute_capture_output(
      {"/usr/bin/file", extracted_file}, &file_output);
    if (file_ret != 0) {
      LOG(ERROR) << "Unable to run file on " << file << ", returned" << file_ret;
      extraction_success = false;
      continue;
    }
    if (file_output.find("Android sparse image,") == std::string::npos) {
      continue;
    }
    std::string inflated_file = extracted_file + ".inflated";
    cvd::Command simg_cmd("/usr/bin/simg2img");
    simg_cmd.AddParameter(extracted_file);
    simg_cmd.AddParameter(inflated_file);
    simg_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdOut,
                           cvd::Subprocess::StdIOChannel::kStdErr);
    if (simg_cmd.Start().Wait() != 0) {
      LOG(ERROR) << "Unable to run simg2img on " << file;
      extraction_success = false;
      continue;
    }
    auto rename_ret = rename(inflated_file.c_str(), extracted_file.c_str());
    if (rename_ret != 0) {
      LOG(ERROR) << "Unable to rename deflated version of " << file;
      extraction_success = false;
    }
  }
  for (auto& file : files) {
    file = target_directory + "/" + file;
  }
  return extraction_success ? files : std::vector<std::string>{};
}
