/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

/**
 * Functions for manipulating disk files given to crosvm or QEMU.
 */

#include <stdint.h>

#include <string>
#include <vector>

namespace cuttlefish {

enum ImagePartitionType {
  kLinuxFilesystem = 0,
  kEfiSystemPartition,
};

struct ImagePartition {
  std::string label;
  std::string image_file_path;
  ImagePartitionType type = kLinuxFilesystem;
};

uint64_t AlignToPartitionSize(uint64_t size);

/**
 * Combine the files in `partition` into a single raw disk file and write it to
 * `output_path`. The raw disk file will have a GUID Partition Table and copy in
 * the contents of the files mentioned in `partitions`.
 */
void AggregateImage(const std::vector<ImagePartition>& partitions,
                    const std::string& output_path);

/**
 * Generate the files necessary for booting with a Composite Disk.
 *
 * Composite Disk is a crosvm disk format that is a layer of indirection over
 * other disk files. The Composite Disk file lists names and offsets in the
 * virtual disk.
 *
 * For a complete single disk inside the VM, there must also be a GUID Partition
 * Table header and footer. These are saved to `header_file` and `footer_file`,
 * then the specification file containing the file paths and offsets is saved to
 * `output_composite_path`.
 */
void CreateCompositeDisk(std::vector<ImagePartition> partitions,
                         const std::string& header_file,
                         const std::string& footer_file,
                         const std::string& output_composite_path,
                         bool read_only);
}
