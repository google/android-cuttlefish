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
                         const std::string& vendor_dlkm_build_dir,
                         const std::string& system_dlkm_build_dir);

bool WriteFsConfig(const char* output_path, const std::string& fs_root,
                   const std::string& mount_point);

bool GenerateFileContexts(const char* output_path,
                          const std::string& mount_point);

bool RepackSuperWithPartition(const std::string& superimg_path,
                              const std::string& image_path,
                              const std::string& partition_name);

bool BuildVendorDLKM(const std::string& src_dir, const bool is_erofs,
                     const std::string& output_image);
bool BuildSystemDLKM(const std::string& src_dir, const bool is_erofs,
                     const std::string& output_image);

bool BuildVbmetaImage(const std::string& vendor_dlkm_img,
                      const std::string& vbmeta_path);
bool BuildDlkmImage(const std::string& src_dir, const bool is_erofs,
                    const std::string& partition_name,
                    const std::string& output_image);

// Move file `src` to `dst` if the contents of these files differ.
// Return true if and only if the move happened.
bool MoveIfChanged(const std::string& src, const std::string& dst);

}  // namespace cuttlefish
