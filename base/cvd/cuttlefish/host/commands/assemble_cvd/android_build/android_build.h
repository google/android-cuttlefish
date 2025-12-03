//
// Copyright (C) 2025 The Android Open Source Project
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

#include <functional>
#include <optional>
#include <ostream>
#include <set>
#include <string_view>

#include "absl/strings/str_format.h"
#include "fmt/ostream.h"

#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

/**
 * Represents an Android build, as defined by image files and some of the
 * metadata text files produced by the build system.
 *
 * The build system produces a subset of these files with faster `m` builds, and
 * does the complete set and packages them up into zip files in slower `m dist`
 * builds. The zip files are uploaded to the Android Build server, and the `cvd
 * fetch` tool can download subsets of them to the local filesystem.
 *
 * Image files also contain some duplication: the 'super' image contains logical
 * partitions that may also be present as standalone image files, depending on
 * the file subset available.
 *
 * Note the distinction between "images" and "partitions" in methods. Image
 * files may contain zero or more partitions.
 *
 * Instances of this class present a subset of the files produced by the build
 * system. This may be complete or incomplete subset.
 */
class AndroidBuild {
 public:
  virtual ~AndroidBuild() = default;

  /**
   * Image information, as reported by the Android build system.
   *
   * An image may be one of three different categories:
   * - A partition in the top-level GPT, such as the the `super` partition.
   * - A logical partition stored inside the GPT `super` partition.
   * - A `super_empty` pseudo-partition file that reports what should be in the
   *   `super` partition, but without the logical partition contents.
   */
  virtual Result<std::set<std::string, std::less<void>>> Images();
  /*
   * A file on the host that represents an image. If the file is not already
   * stored in a distinct file on the host, it is first saved to `extract_dir`
   * and returned from there. If the file needs to be extracted and
   * `extract_dir` is not provided, returns an error.
   *
   * It's possible for there to be an image file in `Images()` that cannot be
   * extracted to the filesystem, if a metadata file reports that an image or
   * partition should exist, but it's not actually present anywhere.
   */
  virtual Result<std::string> ImageFile(
      std::string_view name, std::optional<std::string_view> extract_dir = {});

  virtual Result<std::set<std::string, std::less<void>>> AbPartitions();

  /**
   * If this build is combined with another build by mixing system and vendor
   * from different places, reports which partitions this build expects to
   * contribute to a particular side of the mix. System and vendor partition
   * sets should be disjoint.
   */
  virtual Result<std::set<std::string, std::less<void>>> SystemPartitions();
  virtual Result<std::set<std::string, std::less<void>>> VendorPartitions();

  /** Partitions in the super image. Disjoint from GPT entries. */
  virtual Result<std::set<std::string, std::less<void>>> LogicalPartitions();
  /** Entries in the GPT. Disjoint from logical partitions. */
  virtual Result<std::set<std::string, std::less<void>>> PhysicalPartitions();

 private:
  virtual std::ostream& Format(std::ostream&) const = 0;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const AndroidBuild& provider) {
    sink.Append(absl::FormatStreamed(provider));
  }

  friend std::ostream& operator<<(std::ostream&, const AndroidBuild&);
};

}  // namespace cuttlefish

namespace fmt {

template <>
struct formatter<::cuttlefish::AndroidBuild> : ostream_formatter {};

}  // namespace fmt
