//
// Copyright (C) 2019 The Android Open Source Project
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

#include "curl_wrapper.h"

class Artifact {
  std::string name;
  size_t size;
  unsigned long last_modified_time;
  std::string md5;
  std::string content_type;
  std::string revision;
  unsigned long creation_time;
  unsigned int crc32;
public:
  Artifact(const Json::Value&);

  const std::string& Name() const { return name; }
  size_t Size() const { return size; }
  unsigned long LastModifiedTime() const { return last_modified_time; }
  const std::string& Md5() const { return md5; }
  const std::string& ContentType() const { return content_type; }
  const std::string& Revision() const { return revision; }
  unsigned long CreationTime() const { return creation_time; }
  unsigned int Crc32() const { return crc32; }
};

class BuildApi {
  CurlWrapper curl;
  // TODO credential fetcher
public:
  BuildApi() = default;
  ~BuildApi() = default;

  std::string LatestBuildId(const std::string& branch,
                            const std::string& target);

  std::vector<Artifact> Artifacts(const std::string& build_id,
                                  const std::string& target,
                                  const std::string& attempt_id);

  bool ArtifactToFile(const std::string& build_id, const std::string& target,
                      const std::string& attempt_id,
                      const std::string& artifact, const std::string& path);
};
