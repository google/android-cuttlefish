/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <string>
#include "common/libs/utils/result.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace vm_manager {

class CrosvmDisplayController {
 public:
  CrosvmDisplayController(const CuttlefishConfig* config) : config_(config) {};
  Result<int> Add(const int instance_num,
                  const std::vector<CuttlefishConfig::DisplayConfig>&
                      display_configs) const;
  Result<int> Remove(const int instance_num,
                     const std::vector<std::string> display_ids) const;
  Result<std::string> List(const int instance_num);

 private:
  const CuttlefishConfig* config_;
  Result<int> RunCrosvmDisplayCommand(const int instance_num,
                                      const std::vector<std::string>& args,
                                      std::string* stdout_str) const;
};

Result<CrosvmDisplayController> GetCrosvmDisplayController();

}  // namespace vm_manager
}  // namespace cuttlefish
