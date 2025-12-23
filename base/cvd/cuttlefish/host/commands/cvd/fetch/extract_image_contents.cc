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

#include "cuttlefish/host/commands/cvd/fetch/extract_image_contents.h"

#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/archive.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::vector<std::string>> ExtractImageContents(
    const std::string& image_filepath, const std::string& target_dir,
    const bool keep_archive) {
  if (!IsDirectory(image_filepath)) {
    return CF_EXPECT(
        ExtractArchiveContents(image_filepath, target_dir, keep_archive));
  }

  // The image is already uncompressed. Link or move its contents.
  std::vector<std::string> files;
  const std::function<bool(const std::string&)> file_collector =
      [&files, &image_filepath,
       &target_dir](const std::string& filepath) mutable {
        std::string target_filepath =
            target_dir + "/" + filepath.substr(image_filepath.size() + 1);
        if (!IsDirectory(filepath)) {
          files.push_back(target_filepath);
        }
        return true;
      };
  CF_EXPECT(WalkDirectory(image_filepath, file_collector));

  if (keep_archive) {
    // Must use hard linking due to the way fetch_cvd uses the cache.
    CF_EXPECT(HardLinkDirecoryContentsRecursively(image_filepath, target_dir));
  } else {
    CF_EXPECT(MoveDirectoryContents(image_filepath, target_dir));
    // Ignore even if removing directory fails - harmless.
    // TODO: b/471069557 - diagnose unused
    Result<void> unused = RecursivelyRemoveDirectory(image_filepath);
  }
  return files;
}

}  // namespace cuttlefish
