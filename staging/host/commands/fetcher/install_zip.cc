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

#include "install_zip.h"

#include <stdlib.h>

#include <string>
#include <vector>

#include <android-base/strings.h>
#include <android-base/logging.h>

#include "common/libs/utils/archive.h"
#include "common/libs/utils/subprocess.h"

std::vector<std::string> ExtractImages(const std::string& archive_file,
                                       const std::string& target_directory,
                                       const std::vector<std::string>& images) {
  cuttlefish::Archive archive(archive_file);
  bool extracted =
      images.size() > 0
          ? archive.ExtractFiles(images, target_directory)
          : archive.ExtractAll(target_directory);
  if (!extracted) {
    LOG(ERROR) << "Unable to extract images.";
    return {};
  }

  std::vector<std::string> files =
      images.size() > 0 ? std::move(images) : archive.Contents();
  auto it = files.begin();
  while (it != files.end()) {
    if (*it == "" || android::base::EndsWith(*it, "/")) {
      it = files.erase(it);
    } else {
      *it = target_directory + "/" + *it;
      it++;
    }
  }
  return files;
}
