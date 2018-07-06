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

#include <memory>
#include <string>

namespace vm_manager {

// Superclass of every guest VM manager. It provides a static getter that
// chooses the best subclass to instantiate based on the capabilities supported
// by the host packages.
class VmManager {
 public:
  // Returns the most suitable vm manager as a singleton.
  static std::shared_ptr<VmManager> Get();
  virtual ~VmManager() = default;

  virtual bool Start() const = 0;
  virtual bool Stop() const = 0;

  virtual bool EnsureInstanceDirExists() const = 0;
  virtual bool CleanPriorFiles() const = 0;
};

}  // namespace vm_manager
