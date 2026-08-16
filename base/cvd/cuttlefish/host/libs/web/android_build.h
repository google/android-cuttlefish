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
#include <variant>
#include <vector>

#include "cuttlefish/host/libs/web/android_build_string.h"
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
  std::string generation;
  std::string md5;
  std::optional<uint64_t> size;
};

// The objects under a `gs://` prefix, or the single object a `gs://` URL
// names.
struct GcsBuild {
  static Result<GcsBuild> FromBuildString(const GcsBuildString& build_string);

  std::string bucket;
  std::string prefix;                 // ends with '/', empty at the bucket root
  std::optional<std::string> object;  // set in the object form only
  // The listing of the directory form, by artifact name.
  std::map<std::string, GcsObjectInfo> contents;
  std::optional<std::string> generation;
  std::optional<std::string> md5;
  std::optional<uint64_t> size;
  std::optional<std::string> sha256;

  // Derived from the URL for the code that handles every build alike. Never
  // used to address the objects themselves.
  std::string id;
  std::string target;
  std::string product;
  std::optional<std::string> filepath;
};

std::ostream& operator<<(std::ostream&, const GcsBuild&);

// The artifacts of a `gs://` directory build, in the order the listing keeps
// them.
std::vector<std::string> GcsArtifactNames(const GcsBuild& build);

// The same two forms over `https://`, where a pre-signed URL carries its
// credential in the query string of `url`.
struct HttpBuild {
  static Result<HttpBuild> FromBuildString(const HttpBuildString& build_string);

  std::string url;                    // object form, query included
  std::string base;                   // directory form, ends with '/'
  std::optional<std::string> object;  // set in the object form only
  std::optional<std::string> etag;
  // Whether the probe found an origin that serves range requests, without
  // which a member cannot be read out of an archive.
  bool accept_ranges = false;
  std::optional<uint64_t> size;
  std::optional<std::string> sha256;

  // `id` has no query string, so requests must go to `url` or `base`.
  std::string id;
  std::string target;
  std::string product;
  std::optional<std::string> filepath;
};

std::ostream& operator<<(std::ostream&, const HttpBuild&);

using Build = std::variant<DeviceBuild, DirectoryBuild, GcsBuild, HttpBuild>;

std::ostream& operator<<(std::ostream&, const Build&);

std::string FetchLabel(const Build& build);

}  // namespace cuttlefish
