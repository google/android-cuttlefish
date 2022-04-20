//
// Copyright (C) 2022 The Android Open Source Project
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

#include <chrono>
#include <string>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/image_aggregator/image_aggregator.h"

namespace cuttlefish {

class DiskBuilder {
 public:
  DiskBuilder& Partitions(std::vector<ImagePartition> partitions) &;
  DiskBuilder Partitions(std::vector<ImagePartition> partitions) &&;

  DiskBuilder& HeaderPath(std::string header_path) &;
  DiskBuilder HeaderPath(std::string header_path) &&;

  DiskBuilder& FooterPath(std::string footer_path) &;
  DiskBuilder FooterPath(std::string footer_path) &&;

  DiskBuilder& CrosvmPath(std::string crosvm_path) &;
  DiskBuilder CrosvmPath(std::string crosvm_path) &&;

  DiskBuilder& VmManager(std::string vm_manager) &;
  DiskBuilder VmManager(std::string vm_manager) &&;

  DiskBuilder& ConfigPath(std::string config_path) &;
  DiskBuilder ConfigPath(std::string config_path) &&;

  DiskBuilder& CompositeDiskPath(std::string composite_disk_path) &;
  DiskBuilder CompositeDiskPath(std::string composite_disk_path) &&;

  DiskBuilder& OverlayPath(std::string overlay_path) &;
  DiskBuilder OverlayPath(std::string overlay_path) &&;

  DiskBuilder& ResumeIfPossible(bool resume_if_possible) &;
  DiskBuilder ResumeIfPossible(bool resume_if_possible) &&;

  Result<bool> WillRebuildCompositeDisk();
  /** Returns `true` if the file was actually rebuilt. */
  Result<bool> BuildCompositeDiskIfNecessary();
  /** Returns `true` if the file was actually rebuilt. */
  Result<bool> BuildOverlayIfNecessary();

 private:
  Result<std::string> TextConfig();

  std::vector<ImagePartition> partitions_;
  std::string header_path_;
  std::string footer_path_;
  std::string vm_manager_;
  std::string crosvm_path_;
  std::string config_path_;
  std::string composite_disk_path_;
  std::string overlay_path_;
  bool resume_if_possible_;
};

}  // namespace cuttlefish
