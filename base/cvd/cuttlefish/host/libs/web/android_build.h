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

#include <stdint.h>

#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/android_build_url.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct DeviceBuild {
  std::string id;
  std::string branch;
  std::string target;
  std::string product;
  bool is_signed = false;
  // did retrieving build details block waiting for a terminal status
  bool status_blocked = false;
  std::optional<std::string> filepath;
  SafeLevel safe_level;
};

std::ostream& operator<<(std::ostream&, const DeviceBuild&);

struct DirectoryBuild {
  DirectoryBuild(std::vector<std::string> paths, std::string target,
                 std::optional<std::string> filepath);

  std::vector<std::string> paths;
  std::string target;
  std::string id;
  std::string product;
  bool is_signed = false;
  std::optional<std::string> filepath;
};

std::ostream& operator<<(std::ostream&, const DirectoryBuild&);

struct GcsObjectInfo {
  std::optional<std::string> generation;
  std::optional<std::string> md5;
  std::optional<uint64_t> size;
};

// Identifies the objects under a `gs://` prefix, or the single object a
// `gs://` URL names.
struct GcsBuild {
  static Result<GcsBuild> FromBuildString(const GcsBuildString& build_string);

  std::string bucket;
  std::string prefix;                 // ends with '/', empty at the bucket root
  std::optional<std::string> object;  // set in the object form only
  std::map<std::string, GcsObjectInfo> contents;
  std::optional<std::string> generation;
  std::optional<std::string> md5;
  std::optional<uint64_t> size;
  std::optional<std::string> sha256;
  std::string id;
  std::string target;
  std::string product;
  std::optional<std::string> filepath;
};

std::ostream& operator<<(std::ostream&, const GcsBuild&);

// Returns the artifacts of a `gs://` directory build, in name order.
std::vector<std::string> GcsArtifactNames(const GcsBuild& build);

// Identifies the same two forms over `https://`, where a pre-signed URL
// carries its credential in the query string of `url`. `id` drops that query
// string, so requests use `url`.
struct HttpBuild {
  static Result<HttpBuild> FromBuildString(const HttpBuildString& build_string);

  std::string url;                    // object, or directory ending in '/'
  std::optional<std::string> object;  // set in the object form only
  std::optional<std::string> etag;
  bool accept_ranges = false;
  std::optional<uint64_t> size;
  std::optional<std::string> sha256;
  std::string id;
  std::string target;
  std::string product;
  std::optional<std::string> filepath;
};

std::ostream& operator<<(std::ostream&, const HttpBuild&);

using Build = std::variant<DeviceBuild, DirectoryBuild, GcsBuild, HttpBuild>;

std::ostream& operator<<(std::ostream&, const Build&);

std::string FetchLabel(const Build& build);

// Returns the digest the build string asked `artifact_name` to have, if any.
std::optional<std::string> ArtifactSha256(const Build& build,
                                          const std::string& artifact_name);

// Returns whether the build's namespace holds `artifact_name`. Only a listed
// namespace can answer "no"; the others answer by attempting the download.
bool BuildHasArtifact(const Build& build, const std::string& artifact_name);

// Returns the path safe cache directory of one artifact of one build,
// relative to the cache root. Android Build and directory builds key on
// "{id}/{target}"; URL builds add the version the source reports for that one
// artifact, so that overwriting an object never serves the bytes it replaced.
// Returns nullopt when the source reports no version, which means the
// artifact must not be cached.
std::optional<std::string> BuildCacheKey(const Build& build,
                                         const std::string& artifact_name);

std::tuple<std::string, std::string> GetBuildIdAndTarget(const Build& build);

std::optional<std::string> GetFilepath(const Build& build);

std::string ConstructTargetFilepath(const std::string& directory,
                                    const std::string& filename);

}  // namespace cuttlefish
