/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <host/libs/config/cuttlefish_config.h>

namespace vm_manager {

// Superclass of every guest VM manager. It provides a static getter that
// returns the requested vm manager as a singleton.
class VmManager {
 public:
  // Returns the most suitable vm manager as a singleton. It may return nullptr
  // if the requested vm manager is not supported by the current version of the
  // host packages
  static VmManager* Get(const std::string& vm_manager_name,
                        vsoc::CuttlefishConfig* config);
  static bool IsValidName(const std::string& name);
  static bool IsVmManagerSupported(const std::string& name);
  static std::vector<std::string> GetValidNames();

  virtual ~VmManager() = default;

  virtual bool Start() const = 0;
  virtual bool Stop() const = 0;

  virtual bool EnsureInstanceDirExists() const = 0;
  virtual bool CleanPriorFiles() const = 0;

  virtual bool ValidateHostConfiguration(
      std::vector<std::string>* config_commands) const = 0;

 protected:
  static bool UserInGroup(const std::string& group,
                          std::vector<std::string>* config_commands);
  vsoc::CuttlefishConfig* config_;
  VmManager(vsoc::CuttlefishConfig* config);

private:
  // Holds a map of manager names to a pair of functions. The first function
  // implements a singleton for the specified manager and the second one
  // specifies whether the host packages support it.
 using Builder = std::function<VmManager*(vsoc::CuttlefishConfig*)>;
 using SupportChecker = std::function<bool()>;
 using VmManagerHelper = std::pair<Builder, SupportChecker>;
 static std::map<std::string, VmManagerHelper> vm_manager_helpers_;
};

}  // namespace vm_manager
