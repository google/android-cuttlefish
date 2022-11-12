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

#include "host/libs/config/esp.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/files.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

bool NewfsMsdos(const std::string& data_image, int data_image_mb,
                int offset_num_mb) {
  off_t image_size_bytes = static_cast<off_t>(data_image_mb) << 20;
  off_t offset_size_bytes = static_cast<off_t>(offset_num_mb) << 20;
  image_size_bytes -= offset_size_bytes;
  off_t image_size_sectors = image_size_bytes / 512;
  auto newfs_msdos_path = HostBinaryPath("newfs_msdos");
  return execute({newfs_msdos_path,
                  "-F",
                  "32",
                  "-m",
                  "0xf8",
                  "-o",
                  "0",
                  "-c",
                  "8",
                  "-h",
                  "255",
                  "-u",
                  "63",
                  "-S",
                  "512",
                  "-s",
                  std::to_string(image_size_sectors),
                  "-C",
                  std::to_string(data_image_mb) + "M",
                  "-@",
                  std::to_string(offset_size_bytes),
                  data_image}) == 0;
}

bool MsdosMakeDirectories(const std::string& image_path,
                          const std::vector<std::string>& directories) {
  auto mmd = HostBinaryPath("mmd");
  std::vector<std::string> command {mmd, "-i", image_path};
  command.insert(command.end(), directories.begin(), directories.end());

  const auto success = execute(command);
  if (success != 0) {
    return false;
  }
  return true;
}

bool CopyToMsdos(const std::string& image, const std::string& path,
                 const std::string& destination) {
  const auto mcopy = HostBinaryPath("mcopy");
  const auto success = execute({mcopy, "-o", "-i", image, "-s", path, destination});
  if (success != 0) {
    return false;
  }
  return true;
}

EspBuilder& EspBuilder::File(std::string from, std::string to, bool required) & {
  files_.push_back(FileToAdd {std::move(from), std::move(to), required});
  return *this;
}

EspBuilder EspBuilder::File(std::string from, std::string to, bool required) && {
  files_.push_back(FileToAdd {std::move(from), std::move(to), required});
  return *this;
}

EspBuilder& EspBuilder::File(std::string from, std::string to) & {
  return File(std::move(from), std::move(to), false);
}

EspBuilder EspBuilder::File(std::string from, std::string to) && {
  return File(std::move(from), std::move(to), false);
}

EspBuilder& EspBuilder::Directory(std::string path) & {
  directories_.push_back(std::move(path));
  return *this;
}

EspBuilder EspBuilder::Directory(std::string path) && {
  directories_.push_back(std::move(path));
  return *this;
}

bool EspBuilder::Build() const {
  // newfs_msdos won't make a partition smaller than 257 mb
  // this should be enough for anybody..
  const auto tmp_esp_image = image_path_ + ".tmp";
  if (!NewfsMsdos(tmp_esp_image, 257 /* mb */, 0 /* mb (offset) */)) {
    LOG(ERROR) << "Failed to create filesystem for " << tmp_esp_image;
    return false;
  }

  if (!MsdosMakeDirectories(tmp_esp_image, directories_)) {
    LOG(ERROR) << "Failed to create directories in " << tmp_esp_image;
    return false;
  }

  for (const auto file : files_) {
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

  if (!RenameFile(tmp_esp_image, image_path_)) {
    LOG(ERROR) << "Renaming " << tmp_esp_image << " to "
                << image_path_ << " failed";
    return false;
  }

  return true;
}

} // namespace cuttlefish
