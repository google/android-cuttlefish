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
#include <android-base/logging.h>
#include <json/json.h>

#include "common/libs/utils/files.h"

namespace cuttlefish {

namespace {

const char* kFlags = "flags";
const char* kCvdFiles = "cvd_files";
const char* kCvdFileSource = "source";
const char* kCvdFileBuildId = "build_id";
const char* kCvdFileBuildTarget = "build_target";

FileSource SourceStringToEnum(std::string source) {
  for (auto& c : source) {
    c = std::tolower(c);
  }
  if (source == "default_build") {
    return FileSource::DEFAULT_BUILD;
  } else if (source == "system_build") {
    return FileSource::SYSTEM_BUILD;
  } else if (source == "kernel_build") {
    return FileSource::KERNEL_BUILD;
  } else if (source == "local_file") {
    return FileSource::LOCAL_FILE;
  } else if (source == "generated") {
    return FileSource::GENERATED;
  } else {
    return FileSource::UNKNOWN_PURPOSE;
  }
}

std::string SourceEnumToString(const FileSource& source) {
  if (source == FileSource::DEFAULT_BUILD) {
    return "default_build";
  } else if (source == FileSource::SYSTEM_BUILD) {
    return "system_build";
  } else if (source == FileSource::KERNEL_BUILD) {
    return "kernel_build";
  } else if (source == FileSource::LOCAL_FILE) {
    return "local_file";
  } else if (source == FileSource::GENERATED) {
    return "generated";
  } else {
    return "unknown";
  }
}

} // namespace

CvdFile::CvdFile() {
}

CvdFile::CvdFile(const FileSource& source, const std::string& build_id,
                 const std::string& build_target, const std::string& file_path)
    : source(source), build_id(build_id), build_target(build_target), file_path(file_path) {
}

std::ostream& operator<<(std::ostream& os, const CvdFile& cvd_file) {
  os << "CvdFile(";
  os << "source = " << SourceEnumToString(cvd_file.source) << ", ";
  os << "build_id = " << cvd_file.build_id << ", ";
  os << "build_target = " << cvd_file.build_target << ", ";
  os << "file_path = " << cvd_file.file_path << ")";
  return os;
}

FetcherConfig::FetcherConfig() : dictionary_(new Json::Value()) {
}

FetcherConfig::FetcherConfig(FetcherConfig&&) = default;

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
  auto real_file_path = cuttlefish::AbsolutePath(file);
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

namespace {

CvdFile JsonToCvdFile(const std::string& file_path, const Json::Value& json) {
  CvdFile cvd_file;
  cvd_file.file_path = file_path;
  if (json.isMember(kCvdFileSource)) {
    cvd_file.source = SourceStringToEnum(json[kCvdFileSource].asString());
  } else {
    cvd_file.source = UNKNOWN_PURPOSE;
  }
  if (json.isMember(kCvdFileBuildId)) {
    cvd_file.build_id = json[kCvdFileBuildId].asString();
  }
  if (json.isMember(kCvdFileBuildTarget)) {
    cvd_file.build_target = json[kCvdFileBuildTarget].asString();
  }
  return cvd_file;
}

Json::Value CvdFileToJson(const CvdFile& cvd_file) {
  Json::Value json;
  json[kCvdFileSource] = SourceEnumToString(cvd_file.source);
  json[kCvdFileBuildId] = cvd_file.build_id;
  json[kCvdFileBuildTarget] = cvd_file.build_target;
  return json;
}

} // namespace

bool FetcherConfig::add_cvd_file(const CvdFile& file, bool override_entry) {
  if (!dictionary_->isMember(kCvdFiles)) {
    Json::Value files_json(Json::objectValue);
    (*dictionary_)[kCvdFiles] = files_json;
  }
  if ((*dictionary_)[kCvdFiles].isMember(file.file_path) && !override_entry) {
    return false;
  }
  (*dictionary_)[kCvdFiles][file.file_path] = CvdFileToJson(file);
  return true;
}

std::map<std::string, CvdFile> FetcherConfig::get_cvd_files() const {
  if (!dictionary_->isMember(kCvdFiles)) {
    return {};
  }
  std::map<std::string, CvdFile> files;
  const auto& json_files = (*dictionary_)[kCvdFiles];
  for (auto it = json_files.begin(); it != json_files.end(); it++) {
    files[it.key().asString()] = JsonToCvdFile(it.key().asString(), *it);
  }
  return files;
}

std::string FetcherConfig::FindCvdFileWithSuffix(const std::string& suffix) const {
  if (!dictionary_->isMember(kCvdFiles)) {
    return {};
  }
  const auto& json_files = (*dictionary_)[kCvdFiles];
  for (auto it = json_files.begin(); it != json_files.end(); it++) {
    auto file = it.key().asString();
    auto expected_pos = file.size() - suffix.size();
    if (file.rfind(suffix) == expected_pos) {
      return file;
    }
  }
  LOG(DEBUG) << "Could not find file ending in " << suffix;
  return "";
}

} // namespace cuttlefish
