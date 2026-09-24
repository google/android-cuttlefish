//
// Copyright (C) 2026 The Android Open Source Project
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
#include <string_view>

#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

// Resolution of a config / command line argument that accepts a path in
// system_image_dir or an absolute path.
//
// Shared rules for the helpers below:
//   * An empty value means "not set" and is returned unchanged.
//   * A path beginning with '/' is already absolute: it is returned unchanged.
//   * A relative path is checked for existence: ResolveSystemImageDirPath and
//     ResolveCrosvmDeviceTreeOverlayPath resolve against system_image_dir;
//     ResolveCrosvmFileBackedMappingPath checks system_image_dir first, then
//     falls back to DefaultHostArtifactsPath. If the file does not exist, an
//     error is returned.
Result<std::string> ResolveSystemImageDirPath(std::string_view value,
                                              std::string_view system_image_dir,
                                              std::string_view flag_name);

// Resolves the path in --device-tree-overlay for passing to crosvm
// The expected value should be in the same format as crosvm's
// device-tree-overlay command line argument:
// "PATH[,filter][,select-symbols=[xxx,yyy]]".
// Only the first part (PATH) is checked and rewritten.
Result<std::string> ResolveCrosvmDeviceTreeOverlayPath(
    std::string_view value, std::string_view system_image_dir);

// Resolves the path in --file-backed-mapping for passing to crosvm.
// The expected value should be in the same format as crosvm's
// file-backed-mapping command line argument: "path=X,addr=0x...,size=0x...".
// Only the value of the `path=` token is checked and rewritten, others are
// untouched.
// Relative paths are checked first in system_image_dir, and if not found there,
// checked in DefaultHostArtifactsPath.
Result<std::string> ResolveCrosvmFileBackedMappingPath(
    std::string_view value, std::string_view system_image_dir);

}  // namespace cuttlefish
