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

#include "cuttlefish/host/libs/config/fetcher_config.h"

#include <cctype>
#include <cstddef>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>
#include "absl/strings/match.h"
#include "absl/strings/str_replace.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/file_source.h"

namespace cuttlefish {

namespace {

const char* kCvdFiles = "cvd_files";
const char* kCvdFileSource = "source";
const char* kCvdFileBuildId = "build_id";
const char* kCvdFileBuildTarget = "build_target";
const char* kCvdFileArchiveSource = "archive_source";
const char* kCvdFileArchivePath = "archive_path";

}  // namespace

CvdFile::CvdFile() {}

CvdFile::CvdFile(FileSource source, std::string build_id,
                 std::string build_target, std::string file_path,
                 std::string archive_source, std::string archive_path)
    : source(source),
      build_id(std::move(build_id)),
      build_target(std::move(build_target)),
      file_path(std::move(file_path)),
      archive_source(std::move(archive_source)),
      archive_path(std::move(archive_path)) {}

std::ostream& operator<<(std::ostream& os, const CvdFile& cvd_file) {
  os << "CvdFile(";
  os << "source = " << SourceEnumToString(cvd_file.source) << ", ";
  os << "build_id = " << cvd_file.build_id << ", ";
  os << "build_target = " << cvd_file.build_target << ", ";
  os << "file_path = " << cvd_file.file_path << ",";
  os << "archive_source = " << cvd_file.archive_source << ",";
  os << "archive_path = " << cvd_file.archive_path << ")";
  return os;
}

FetcherConfig::FetcherConfig() : mutex_(std::make_unique<std::mutex>()) {}

FetcherConfig::FetcherConfig(FetcherConfig&& other) {
  dictionary_ = std::move(other.dictionary_);
  other.dictionary_ = Json::Value();

  mutex_ = std::move(other.mutex_);
  other.mutex_ = std::make_unique<std::mutex>();
}

FetcherConfig& FetcherConfig::operator=(FetcherConfig&& other) {
  dictionary_ = std::move(other.dictionary_);
  other.dictionary_ = Json::Value();

  mutex_ = std::move(other.mutex_);
  other.mutex_ = std::make_unique<std::mutex>();

  return *this;
}

bool FetcherConfig::SaveToFile(const std::string& file) const {
  std::scoped_lock lock(*mutex_);

  std::ofstream ofs(file);
  if (!ofs.is_open()) {
    LOG(ERROR) << "Unable to write to file " << file;
    return false;
  }
  ofs << dictionary_;
  return !ofs.fail();
}

bool FetcherConfig::LoadFromFile(const std::string& file) {
  std::scoped_lock lock(*mutex_);

  auto real_file_path = AbsolutePath(file);
  if (real_file_path.empty()) {
    LOG(ERROR) << "Could not get real path for file " << file;
    return false;
  }
  Json::CharReaderBuilder builder;
  std::ifstream ifs(real_file_path);
  std::string errorMessage;
  if (!Json::parseFromStream(builder, ifs, &dictionary_, &errorMessage)) {
    LOG(ERROR) << "Could not read config file " << file << ": " << errorMessage;
    return false;
  }

  auto base_dir = android::base::Dirname(file);
  if (base_dir != "." && dictionary_.isMember(kCvdFiles)) {
    LOG(INFO) << "Adjusting cvd_file paths to directory: " << base_dir;
    for (const auto& member_name : dictionary_[kCvdFiles].getMemberNames()) {
      dictionary_[kCvdFiles][base_dir + "/" + member_name] =
          dictionary_[kCvdFiles][member_name];
      dictionary_[kCvdFiles].removeMember(member_name);
    }
  }

  return true;
}

namespace {

CvdFile JsonToCvdFile(const std::string& file_path, const Json::Value& json) {
  CvdFile cvd_file;
  cvd_file.file_path = file_path;
  if (json.isMember(kCvdFileSource)) {
    cvd_file.source = SourceStringToEnum(json[kCvdFileSource].asString());
  } else {
    cvd_file.source = FileSource::UNKNOWN_PURPOSE;
  }
  if (json.isMember(kCvdFileBuildId)) {
    cvd_file.build_id = json[kCvdFileBuildId].asString();
  }
  if (json.isMember(kCvdFileBuildTarget)) {
    cvd_file.build_target = json[kCvdFileBuildTarget].asString();
  }
  if (json.isMember(kCvdFileArchiveSource)) {
    cvd_file.archive_source = json[kCvdFileArchiveSource].asString();
  }
  if (json.isMember(kCvdFileArchivePath)) {
    cvd_file.archive_path = json[kCvdFileArchivePath].asString();
  }
  return cvd_file;
}

Json::Value CvdFileToJson(const CvdFile& cvd_file) {
  Json::Value json;
  json[kCvdFileSource] = SourceEnumToString(cvd_file.source);
  json[kCvdFileBuildId] = cvd_file.build_id;
  json[kCvdFileBuildTarget] = cvd_file.build_target;
  json[kCvdFileArchiveSource] = cvd_file.archive_source;
  json[kCvdFileArchivePath] = cvd_file.archive_path;
  return json;
}

Result<std::string> NormalizePath(std::string path) {
  CF_EXPECT(!absl::StrContains(path, ".."));
  while (absl::StrContains(path, "//")) {
    absl::StrReplaceAll({{"//", "/"}}, &path);
  }
  return path;
}

}  // namespace

bool FetcherConfig::add_cvd_file(const CvdFile& file, bool override_entry) {
  std::scoped_lock lock(*mutex_);

  if (!dictionary_.isMember(kCvdFiles)) {
    Json::Value files_json(Json::objectValue);
    dictionary_[kCvdFiles] = files_json;
  }
  if (dictionary_[kCvdFiles].isMember(file.file_path) && !override_entry) {
    return false;
  }
  dictionary_[kCvdFiles][file.file_path] = CvdFileToJson(file);
  return true;
}

std::map<std::string, CvdFile> FetcherConfig::get_cvd_files() const {
  std::scoped_lock lock(*mutex_);

  if (!dictionary_.isMember(kCvdFiles)) {
    return {};
  }
  std::map<std::string, CvdFile> files;
  const auto& json_files = dictionary_[kCvdFiles];
  for (auto it = json_files.begin(); it != json_files.end(); it++) {
    files[it.key().asString()] = JsonToCvdFile(it.key().asString(), *it);
  }
  return files;
}

std::string FetcherConfig::FindCvdFileWithSuffix(
    FileSource source, std::string_view suffix) const {
  std::scoped_lock lock(*mutex_);

  if (!dictionary_.isMember(kCvdFiles)) {
    return {};
  }
  const Json::Value& json_files = dictionary_[kCvdFiles];
  for (auto it = json_files.begin(); it != json_files.end(); it++) {
    const std::string& file = it.key().asString();
    if (!absl::EndsWith(file, suffix)) {
      continue;
    }
    CvdFile parsed = JsonToCvdFile(file, *it);
    if (parsed.source != source) {
      continue;
    }
    return file;
  }
  LOG(DEBUG) << "Could not find file ending in " << suffix;
  return "";
}

Result<void> FetcherConfig::RemoveFileFromConfig(const std::string& path) {
  std::scoped_lock lock(*mutex_);

  if (!dictionary_.isMember(kCvdFiles)) {
    return {};
  }
  std::string normalized = CF_EXPECT(NormalizePath(std::string(path)));
  auto& json_files = dictionary_[kCvdFiles];
  CF_EXPECTF(json_files.isMember(normalized), "Unknown file '{}'", normalized);
  json_files.removeMember(normalized);
  return {};
}

Result<CvdFile> BuildFetcherConfigMember(
    FileSource purpose, std::string build_id, std::string build_target,
    std::string path, std::string directory_prefix, std::string archive_source,
    std::string archive_path) {
  std::string_view local_path(path);
  if (!android::base::ConsumePrefix(&local_path, directory_prefix)) {
    LOG(ERROR) << "Failed to remove prefix " << directory_prefix << " from "
               << local_path;
    return {};
  }
  while (android::base::StartsWith(local_path, "/")) {
    android::base::ConsumePrefix(&local_path, "/");
  }
  std::string normalized = CF_EXPECT(NormalizePath(std::string(local_path)));
  // TODO(schuffelen): Do better for local builds here.
  return CvdFile(std::move(purpose), std::move(build_id),
                 std::move(build_target), std::move(normalized),
                 std::move(archive_source), std::move(archive_path));
}

FetcherConfigs FetcherConfigs::Create(std::vector<FetcherConfig> configs) {
  if (configs.empty()) {
    configs.emplace_back();
  }
  return FetcherConfigs(std::move(configs));
}

FetcherConfigs::FetcherConfigs(std::vector<FetcherConfig> configs)
    : fetcher_configs_(std::move(configs)) {}

void FetcherConfigs::Append(FetcherConfig&& config) {
  fetcher_configs_.emplace_back(std::move(config));
}

const FetcherConfig& FetcherConfigs::ForInstance(size_t instance_index) const {
  if (instance_index < fetcher_configs_.size()) {
    return fetcher_configs_[instance_index];
  }
  return fetcher_configs_[0];
}

}  // namespace cuttlefish
