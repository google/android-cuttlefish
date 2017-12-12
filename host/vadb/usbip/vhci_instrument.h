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

#include "common/libs/fs/shared_fd.h"

namespace vadb {
namespace usbip {
// VHCIInstrument class configures VHCI-HCD on local kernel.
class VHCIInstrument {
 public:
  VHCIInstrument(const std::string& name);
  virtual ~VHCIInstrument();

  // Init opens vhci-hcd driver and allocates port to which remote USB device
  // will be attached.
  // Returns false, if vhci-hcd driver could not be opened, or if no free port
  // was found.
  bool Init();

  // TriggerAttach tells underlying thread to make attempt to re-attach USB
  // device.
  void TriggerAttach();

  // TriggerDetach tells underlying thread to disconnect remote USB device.
  void TriggerDetach();

 private:
  // Attach makes an attempt to configure VHCI to enable virtual USB device.
  // Returns true, if configuration attempt was successful.
  bool Attach();

  // Detach disconnects virtual USB device.
  // Returns true, if attempt was successful.
  bool Detach();

  // AttachThread is a background thread that responds to configuration
  // requests.
  void AttachThread();
  bool FindFreePort();

 private:
  std::unique_ptr<udev, void(*)(udev*)> udev_;
  std::unique_ptr<udev_device, void(*)(udev_device*)> vhci_device_;
  std::string name_;
  std::unique_ptr<std::thread> attach_thread_;
  std::string syspath_;
  avd::SharedFD control_write_end_;
  avd::SharedFD control_read_end_;
  avd::SharedFD vhci_socket_;
  int port_;

  VHCIInstrument(const VHCIInstrument& other) = delete;
  VHCIInstrument& operator=(const VHCIInstrument& other) = delete;
};
}  // namespace usbip
}  // namespace vadb
