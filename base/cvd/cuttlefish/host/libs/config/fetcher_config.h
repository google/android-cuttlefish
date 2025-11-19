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
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "json/value.h"

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/file_source.h"

namespace cuttlefish {

/*
 * Attempts to answer the general question "where did this file come from, and
 * what purpose is it serving?
 */
struct CvdFile {
  FileSource source;
  std::string build_id;
  std::string build_target;
  std::string file_path;
  /* If `cvd fetch` extracted this file from an archive, what was the name of
   * that archive? */
  std::string archive_source;
  /* What was the path that the file was stored at in the archive? */
  std::string archive_path;

  CvdFile();
  CvdFile(FileSource source, std::string build_id, std::string build_target,
          std::string file_path, std::string archive_source,
          std::string archive_path);
};

std::ostream& operator<<(std::ostream&, const CvdFile&);

/**
 * A report of state to transfer from fetch_cvd to downstream consumers.
 *
 * This includes data intended for programmatic access by other tools such as
 * assemble_cvd. assemble_cvd can use signals like that multiple build IDs are
 * present to judge that it needs to do super image remixing or rebuilding the
 * boot image for a new kernel.
 *
 * The output json also includes data relevant for human debugging, like which
 * flags fetch_cvd was invoked with.
 *
 * `FetcherConfig` is thread-safe for reads and writes, but the move constructor
 * and assignment operator are not thread-safe and cannot be called concurrently
 * with any other operations.
 */
class FetcherConfig {
 public:
  FetcherConfig();
  FetcherConfig(FetcherConfig&&);
  FetcherConfig& operator=(FetcherConfig&&);

  bool SaveToFile(const std::string& file) const;
  bool LoadFromFile(const std::string& file);

  bool add_cvd_file(const CvdFile& file, bool override_entry = false);
  std::map<std::string, CvdFile> get_cvd_files() const;

  std::string FindCvdFileWithSuffix(FileSource source,
                                    std::string_view suffix) const;

  Result<void> RemoveFileFromConfig(const std::string& path);

 private:
  Json::Value dictionary_;
  std::unique_ptr<std::mutex> mutex_;
};

Result<CvdFile> BuildFetcherConfigMember(
    FileSource purpose, std::string build_id, std::string build_target,
    std::string path, std::string directory_prefix,
    std::string archive_source = "", std::string archive_path = "");

class FetcherConfigs {
 public:
  static FetcherConfigs Create(std::vector<FetcherConfig> configs);
  FetcherConfigs(FetcherConfigs&&) = default;
  ~FetcherConfigs() = default;

  void Append(FetcherConfig&& config);

  size_t Size() const { return fetcher_configs_.size(); }

  const FetcherConfig& ForInstance(size_t instance_index) const;

 private:
  FetcherConfigs(std::vector<FetcherConfig> configs);
  std::vector<FetcherConfig> fetcher_configs_;
};

}  // namespace cuttlefish
