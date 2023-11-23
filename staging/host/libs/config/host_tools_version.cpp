//
// Copyright (C) 2020 The Android Open Source Project
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

#include "host/libs/config/host_tools_version.h"

#include <algorithm>
#include <fstream>
#include <future>
#include <vector>

#include <zlib.h>

#include "common/libs/utils/files.h"
#include "host/libs/config/config_utils.h"

using std::uint32_t;

namespace cuttlefish {

uint32_t FileCrc(const std::string& path) {
  uint32_t crc = crc32(0, (unsigned char*) path.c_str(), path.size());
  std::ifstream file_stream(path, std::ifstream::binary);
  std::vector<char> data(1024, 0);
  while (file_stream) {
    file_stream.read(data.data(), data.size());
    crc = crc32(crc, (unsigned char*) data.data(), file_stream.gcount());
  }
  return crc;
}

static std::map<std::string, uint32_t> DirectoryCrc(const std::string& path) {
  auto full_path = DefaultHostArtifactsPath(path);
  if (!DirectoryExists(full_path)) {
    return {};
  }
  auto files_result = DirectoryContents(full_path);
  CHECK(files_result.ok()) << files_result.error().FormatForEnv();
  std::vector<std::string> files = std::move(*files_result);
  for (auto it = files.begin(); it != files.end();) {
    if (*it == "." || *it == "..") {
      it = files.erase(it);
    } else {
      it++;
    }
  }
  std::vector<std::future<uint32_t>> calculations;
  calculations.reserve(files.size());
  for (auto& file : files) {
    file = path + "/" + file; // mutate in place in files vector
    calculations.emplace_back(
        std::async(FileCrc, DefaultHostArtifactsPath(file)));
  }
  std::map<std::string, uint32_t> crcs;
  for (int i = 0; i < files.size(); i++) {
    crcs[files[i]] = calculations[i].get();
  }
  return crcs;
}

std::map<std::string, uint32_t> HostToolsCrc() {
  auto bin_future = std::async(DirectoryCrc, "bin");
  auto lib_future = std::async(DirectoryCrc, "lib64");
  std::map<std::string, uint32_t> all_crcs;
  for (auto const& [file, crc] : bin_future.get()) {
    all_crcs[file] = crc;
  }
  for (auto const& [file, crc] : lib_future.get()) {
    all_crcs[file] = crc;
  }
  return all_crcs;
}

} // namespace cuttlefish
