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

#include "host/libs/config/fetcher_config.h"

#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <json/json.h>

#include "common/libs/utils/files.h"

namespace {

const char* kFlags = "flags";
const char* kFiles = "files";

} // namespace

namespace cvd {

FetcherConfig::FetcherConfig() : dictionary_(new Json::Value()) {
}

FetcherConfig::~FetcherConfig() {
}

bool FetcherConfig::SaveToFile(const std::string& file) const {
  std::ofstream ofs(file);
  if (!ofs.is_open()) {
    LOG(ERROR) << "Unable to write to file " << file;
    return false;
  }
  ofs << *dictionary_;
  return !ofs.fail();
}

bool FetcherConfig::LoadFromFile(const std::string& file) {
  auto real_file_path = cvd::AbsolutePath(file);
  if (real_file_path.empty()) {
    LOG(ERROR) << "Could not get real path for file " << file;
    return false;
  }
  Json::Reader reader;
  std::ifstream ifs(real_file_path);
  if (!reader.parse(ifs, *dictionary_)) {
    LOG(ERROR) << "Could not read config file " << file << ": "
               << reader.getFormattedErrorMessages();
    return false;
  }
  return true;
}

void FetcherConfig::RecordFlags() {
  std::vector<gflags::CommandLineFlagInfo> all_flags;
  GetAllFlags(&all_flags);
  Json::Value flags_json(Json::arrayValue);
  for (const auto& flag : all_flags) {
    Json::Value flag_json;
    flag_json["name"] = flag.name;
    flag_json["type"] = flag.type;
    flag_json["description"] = flag.description;
    flag_json["current_value"] = flag.current_value;
    flag_json["default_value"] = flag.default_value;
    flag_json["filename"] = flag.filename;
    flag_json["has_validator_fn"] = flag.has_validator_fn;
    flag_json["is_default"] = flag.is_default;
    flags_json.append(flag_json);
  }
  (*dictionary_)[kFlags] = flags_json;
}

void FetcherConfig::set_files(const std::vector<std::string>& files) {
  Json::Value files_json(Json::arrayValue);
  for (const auto& file : files) {
    files_json.append(file);
  }
  (*dictionary_)[kFiles] = files_json;
}

std::vector<std::string> FetcherConfig::files() const {
  if (!dictionary_->isMember(kFiles)) {
    return {};
  }
  std::vector<std::string> files;
  for (const auto& file : (*dictionary_)[kFiles]) {
    files.push_back(file.asString());
  }
  return files;
}

} // namespace cvd
