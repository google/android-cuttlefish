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
#include <ostream>
#include <string>

namespace Json {
class Value;
}

namespace cuttlefish {

// Order in enum is not guaranteed to be stable, serialized as a string.
enum FileSource {
  UNKNOWN_PURPOSE = 0,
  DEFAULT_BUILD,
  SYSTEM_BUILD,
  KERNEL_BUILD,
  LOCAL_FILE,
  GENERATED,
};

/*
 * Attempts to answer the general question "where did this file come from, and
 * what purpose is it serving?
 */
struct CvdFile {
  FileSource source;
  std::string build_id;
  std::string build_target;
  std::string file_path;

  CvdFile();
  CvdFile(const FileSource& source, const std::string& build_id,
          const std::string& build_target, const std::string& file_path);
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
 */
class FetcherConfig {
  std::unique_ptr<Json::Value> dictionary_;
public:
  FetcherConfig();
  FetcherConfig(FetcherConfig&&);
  ~FetcherConfig();

  bool SaveToFile(const std::string& file) const;
  bool LoadFromFile(const std::string& file);

  // For debugging only, not intended for programmatic access.
  void RecordFlags();

  bool add_cvd_file(const CvdFile& file, bool override_entry = false);
  std::map<std::string, CvdFile> get_cvd_files() const;

  std::string FindCvdFileWithSuffix(const std::string& suffix) const;
};

} // namespace cuttlefish
