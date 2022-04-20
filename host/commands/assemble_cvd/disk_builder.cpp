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

#include "host/commands/assemble_cvd/disk_builder.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/image_aggregator/image_aggregator.h"
#include "host/libs/vm_manager/crosvm_manager.h"

namespace cuttlefish {

void create_overlay_image(const CuttlefishConfig& config,
                          std::string overlay_path, bool resume) {
  bool missingOverlay = !FileExists(overlay_path);
  bool newOverlay = FileModificationTime(overlay_path) <
                    FileModificationTime(config.os_composite_disk_path());
  if (missingOverlay || resume || newOverlay) {
    CreateQcowOverlay(config.crosvm_binary(), config.os_composite_disk_path(),
                      overlay_path);
  }
}

static std::chrono::system_clock::time_point LastUpdatedInputDisk(
    const std::vector<ImagePartition>& partitions) {
  std::chrono::system_clock::time_point ret;
  for (auto& partition : partitions) {
    if (partition.label == "frp") {
      continue;
    }

    auto partition_mod_time = FileModificationTime(partition.image_file_path);
    if (partition_mod_time > ret) {
      ret = partition_mod_time;
    }
  }
  return ret;
}

bool DoesCompositeMatchCurrentDiskConfig(
    const std::string& prior_disk_config_path,
    const std::vector<ImagePartition>& partitions) {
  std::string current_disk_config_path = prior_disk_config_path + ".tmp";
  std::ostringstream disk_conf;
  for (auto& partition : partitions) {
    disk_conf << partition.image_file_path << "\n";
  }

  {
    // This file acts as a descriptor of the cuttlefish disk contents in a VMM
    // agnostic way (VMMs used are QEMU and CrosVM at the time of writing). This
    // file is used to determine if the disk config for the pending boot matches
    // the disk from the past boot.
    std::ofstream file_out(current_disk_config_path.c_str(), std::ios::binary);
    file_out << disk_conf.str();
    CHECK(file_out.good()) << "Disk config verification failed.";
  }

  if (!FileExists(prior_disk_config_path) ||
      ReadFile(prior_disk_config_path) != ReadFile(current_disk_config_path)) {
    CHECK(cuttlefish::RenameFile(current_disk_config_path,
                                 prior_disk_config_path))
        << "Unable to delete the old disk config descriptor";
    LOG(DEBUG) << "Disk Config has changed since last boot. Regenerating "
                  "composite disk.";
    return false;
  } else {
    RemoveFile(current_disk_config_path);
    return true;
  }
}

bool ShouldCreateCompositeDisk(const std::string& composite_disk_path,
                               const std::vector<ImagePartition>& partitions) {
  if (!FileExists(composite_disk_path)) {
    return true;
  }

  auto composite_age = FileModificationTime(composite_disk_path);
  return composite_age < LastUpdatedInputDisk(partitions);
}

Result<void> CreateCompositeDiskCommon(
    const std::string& vm_manager, const std::string& header_path,
    const std::string& footer_path,
    const std::vector<ImagePartition> partitions,
    const std::string& output_path) {
  CF_EXPECT(SharedFD::Open(output_path, O_WRONLY | O_CREAT, 0644)->IsOpen(),
            "Could not ensure \"" << output_path << "\" exists");
  if (vm_manager == vm_manager::CrosvmManager::name()) {
    CreateCompositeDisk(partitions, AbsolutePath(header_path),
                        AbsolutePath(footer_path), AbsolutePath(output_path));
  } else {
    // If this doesn't fit into the disk, it will fail while aggregating. The
    // aggregator doesn't maintain any sparse attributes.
    AggregateImage(partitions, output_path);
  }
  return {};
}

}  // namespace cuttlefish
