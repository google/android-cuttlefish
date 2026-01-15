/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <stdint.h>

#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

class CompositeSuperImageBuilder {
 public:
  CompositeSuperImageBuilder& BlockDeviceSize(uint64_t) &;
  CompositeSuperImageBuilder BlockDeviceSize(uint64_t) &&;

  CompositeSuperImageBuilder& SystemPartition(std::string_view name,
                                              std::string_view host_path) &;
  CompositeSuperImageBuilder SystemPartition(std::string_view name,
                                             std::string_view host_path) &&;

  CompositeSuperImageBuilder& VendorPartition(std::string_view name,
                                              std::string_view host_path) &;
  CompositeSuperImageBuilder VendorPartition(std::string_view name,
                                             std::string_view host_path) &&;

  Result<std::string> WriteToDirectory(std::string_view output_dir,
                                       std::string_view file_name,
                                       std::string_view header_name);

 private:
  size_t size_ = 0;
  // Map is from partition name to filename
  std::map<std::string, std::string, std::less<void>> system_partitions_;
  std::map<std::string, std::string, std::less<void>> vendor_partitions_;
};

}  // namespace cuttlefish
