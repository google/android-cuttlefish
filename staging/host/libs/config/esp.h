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

#pragma once

#include <string>
#include <vector>

namespace cuttlefish {

class EspBuilder final {
 public:
  EspBuilder() = delete;
  EspBuilder(std::string image_path): image_path_(std::move(image_path)) {}

  EspBuilder& File(std::string from, std::string to, bool required) &;
  EspBuilder File(std::string from, std::string to, bool required) &&;

  EspBuilder& File(std::string from, std::string to) &;
  EspBuilder File(std::string from, std::string to) &&;

  EspBuilder& Directory(std::string path) &;
  EspBuilder Directory(std::string path) &&;

  bool Build() const;

 private:
  std::string image_path_;

  struct FileToAdd {
    std::string from;
    std::string to;
    bool required;
  };
  std::vector<std::string> directories_;
  std::vector<FileToAdd> files_;
};

bool NewfsMsdos(const std::string& data_image, int data_image_mb,
                int offset_num_mb);

} // namespace cuttlefish
