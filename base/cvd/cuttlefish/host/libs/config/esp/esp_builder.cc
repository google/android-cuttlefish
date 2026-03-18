//
// Copyright (C) 2022 The Android Open Source Project
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

#include "cuttlefish/host/libs/config/esp/esp_builder.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/libs/config/esp/make_fat_image.h"
#include "cuttlefish/host/libs/config/known_paths.h"

namespace cuttlefish {

static bool MsdosMakeDirectories(const std::string& image_path,
                                 const std::vector<std::string>& directories) {
  std::vector<std::string> command{MmdBinary(), "-i", image_path};
  command.insert(command.end(), directories.begin(), directories.end());

  const auto success = Execute(command);
  if (success != 0) {
    return false;
  }
  return true;
}

static bool CopyToMsdos(const std::string& image, const std::string& path,
                        const std::string& destination) {
  const auto success =
      Execute({McopyBinary(), "-o", "-i", image, "-s", path, destination});
  if (success != 0) {
    return false;
  }
  return true;
}

EspBuilder::EspBuilder() {}
EspBuilder::EspBuilder(std::string image_path)
    : image_path_(std::move(image_path)) {}

EspBuilder& EspBuilder::File(std::string from, std::string to,
                             bool required) & {
  files_.push_back(FileToAdd{std::move(from), std::move(to), required});
  return *this;
}

EspBuilder& EspBuilder::File(std::string from, std::string to) & {
  return File(std::move(from), std::move(to), false);
}

EspBuilder& EspBuilder::Directory(std::string path) & {
  directories_.push_back(std::move(path));
  return *this;
}

EspBuilder& EspBuilder::Merge(EspBuilder builder) & {
  std::move(builder.directories_.begin(), builder.directories_.end(),
            std::back_inserter(directories_));
  std::move(builder.files_.begin(), builder.files_.end(),
            std::back_inserter(files_));
  return *this;
}

bool EspBuilder::Build() {
  if (image_path_.empty()) {
    LOG(ERROR) << "Image path is required to build ESP. Empty constructor is "
                  "intended to "
               << "be used only for the merge functionality";
    return false;
  }

  // newfs_msdos won't make a partition smaller than 257 mb
  // this should be enough for anybody..
  const auto tmp_esp_image = image_path_ + ".tmp";
  if (!MakeFatImage(tmp_esp_image, 257 /* mb */, 0 /* mb (offset) */).ok()) {
    LOG(ERROR) << "Failed to create filesystem for " << tmp_esp_image;
    return false;
  }

  if (!MsdosMakeDirectories(tmp_esp_image, directories_)) {
    LOG(ERROR) << "Failed to create directories in " << tmp_esp_image;
    return false;
  }

  for (const FileToAdd& file : files_) {
    if (!FileExists(file.from)) {
      if (file.required) {
        LOG(ERROR) << "Failed to copy " << file.from << " to " << tmp_esp_image
                   << ": File does not exist";
        return false;
      }
      continue;
    }

    if (!CopyToMsdos(tmp_esp_image, file.from, "::" + file.to)) {
      LOG(ERROR) << "Failed to copy " << file.from << " to " << tmp_esp_image
                 << ": mcopy execution failed";
      return false;
    }
  }

  if (!RenameFile(tmp_esp_image, image_path_).ok()) {
    LOG(ERROR) << "Renaming " << tmp_esp_image << " to " << image_path_
               << " failed";
    return false;
  }

  return true;
}

}  // namespace cuttlefish
