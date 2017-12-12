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

#include <mutex>
#include <string>
#include <thread>

#include "common/libs/fs/shared_fd.h"
#include "guest/usbforward/protocol.h"
#include "host/vadb/usb_cmd.h"
#include "host/vadb/usbip/device.h"
#include "host/vadb/usbip/device_pool.h"
#include "host/vadb/usbip/messages.h"
#include "host/vadb/vhci_instrument.h"

namespace vadb {
// VirtualADB is a companion class for USBForwarder, running on Cuttlefish.
// VirtualADB collects list of available USB devices from Cuttlefish and makes
// them available to USB/IP.
//
// Purpose of this class is to connect to USBForwarder and make access to
// remote USB devices possible with help of USB/IP protocol.
class VirtualADB {
 public:
  VirtualADB(const std::string& path) : path_(path) {}
  virtual ~VirtualADB() = default;

  // Initialize this instance of VirtualADB:
  // Connect to remote server and collect list of available USB devices.
  // Returns false, if connection was unsuccessful.
  bool Init();

  // Pool of USB devices available to export.
  const usbip::DevicePool& Pool() const;

 private:
  // Query remote server; populate available USB devices.
  bool PopulateRemoteDevices();

  // Register new device in a device pool.
  void RegisterDevice(const usb_forward::DeviceInfo& dev,
                      const std::vector<usb_forward::InterfaceInfo>& ifaces);

  // Request attach remote USB device.
  bool HandleAttach(uint8_t bus_id, uint8_t dev_id);

  // Execute control request on remote device.
  bool HandleDeviceControlRequest(uint8_t bus_id, uint8_t dev_id,
                                  const usbip::CmdRequest& r, uint32_t deadline,
                                  std::vector<uint8_t> data,
                                  usbip::Device::AsyncTransferReadyCB callback);

  // Execute data request on remote device.
  bool HandleDeviceDataRequest(uint8_t bus_id, uint8_t dev_id, uint8_t endpoint,
                               bool is_host_to_device, uint32_t deadline,
                               std::vector<uint8_t> data,
                               usbip::Device::AsyncTransferReadyCB callback);

  // ExecuteCommand creates command header and executes supplied USBCommand.
  // If execution was successful, command will be stored internally until
  // response arrives.
  bool ExecuteCommand(std::unique_ptr<USBCommand> cmd);

  // ReceiveThread manages incoming data:
  // - reads response header,
  // - find previously executed command whose tag matches tag found in header,
  // - executes OnResponse() and
  // - disposes of that command.
  void ReceiveThread();

  std::string path_;
  avd::SharedFD fd_;
  usbip::DevicePool pool_;

  std::unique_ptr<std::thread> receive_thread_;

  std::mutex commands_mutex_;
  uint32_t tag_ = 0;
  std::map<uint32_t, std::unique_ptr<USBCommand>> commands_;

  VirtualADB(const VirtualADB& other) = delete;
  VirtualADB& operator=(const VirtualADB& other) = delete;
};  // namespace vadbclassVirtualADB

}  // namespace vadb