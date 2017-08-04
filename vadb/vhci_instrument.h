/*
 * Copyright (C) 2017 The Android Open Source Project
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
#include <thread>

#include <libudev.h>

namespace vadb {
class VHCIInstrument {
 public:
  VHCIInstrument(const std::string& name);
  virtual ~VHCIInstrument() = default;

  bool Init();

  bool Attach();
  void AttachThread();
  bool FindFreePort();

 private:
  std::unique_ptr<udev, void(*)(udev*)> udev_;
  std::unique_ptr<udev_device, void(*)(udev_device*)> vhci_device_;
  std::string name_;
  std::unique_ptr<std::thread> attach_thread_;
  std::string syspath_;
  int port_;

  VHCIInstrument(const VHCIInstrument& other) = delete;
  VHCIInstrument& operator=(const VHCIInstrument& other) = delete;
};
}  // namespace vadb
