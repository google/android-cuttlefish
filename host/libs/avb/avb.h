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

#include "common/libs/utils/result.h"

#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <string>

#include <fruit/fruit.h>

#include "common/libs/utils/subprocess.h"

namespace cuttlefish {

// Taken from external/avb/avbtool.py; this define is not in the headers
inline constexpr uint64_t kMaxAvbMetadataSize = 69632ul;

class Avb {
 public:
  Avb(std::string avbtool_path);
  Avb(std::string avbtool_path, std::string algorithm, std::string key);

  /**
   * AddHashFooter - sign and add hash footer to the partition for
   * avb and dm-verity verification
   *
   * @image_path: path to image to sign
   * @partition_name: partition name (without A/B suffix)
   * @partition_size_bytes: partition size (in bytes)
  */
  Result<void> AddHashFooter(const std::string& image_path,
                             const std::string& partition_name,
                             const off_t partition_size_bytes) const;

 private:
  Command GenerateAddHashFooter(const std::string& image_path,
                                const std::string& partition_name,
                                const off_t partition_size_bytes) const;
  std::string avbtool_path_;
  std::string algorithm_;
  std::string key_;
};

std::unique_ptr<Avb> GetDefaultAvb();

fruit::Component<Avb> CuttlefishKeyAvbComponent();

}  // namespace cuttlefish
