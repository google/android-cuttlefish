//
// Copyright (C) 2023 The Android Open Source Project
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

namespace cuttlefish {

bool SplitRamdiskModules(const std::string& ramdisk_path,
                         const std::string& ramdisk_stage_dir,
                         const std::string& vendor_dlkm_build_dir);

bool WriteFsConfig(const char* output_path, const std::string& fs_root,
                   const std::string& mount_point);

bool GenerateFileContexts(const char* output_path,
                          const std::string& mount_point);

bool RepackSuperWithVendorDLKM(const std::string& superimg_path,
                               const std::string& vendor_dlkm_path);

bool BuildVendorDLKM(const std::string& src_dir, const bool is_erofs,
                     const std::string& output_image);

bool RebuildVbmetaVendor(const std::string& vendor_dlkm_img,
                         const std::string& vbmeta_path);

}  // namespace cuttlefish
