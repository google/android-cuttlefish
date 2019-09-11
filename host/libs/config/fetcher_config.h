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
#include <string>
#include <vector>

namespace Json {
class Value;
}

namespace cvd {

class FetcherConfig {
  std::unique_ptr<Json::Value> dictionary_;
public:
  FetcherConfig();
  ~FetcherConfig();

  bool SaveToFile(const std::string& file) const;
  bool LoadFromFile(const std::string& file);

  // For debugging only, not intended for programmatic access.
  void RecordFlags();

  void set_files(const std::vector<std::string>& files);
  std::vector<std::string> files() const;
};

} // namespace cvd
