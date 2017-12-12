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

#include <string>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/usbforward/protocol.h"
#include "host/libs/vadb/usb_cmd.h"
#include "host/libs/usbip/device.h"
#include "host/libs/usbip/device_pool.h"
#include "host/libs/usbip/messages.h"
#include "host/libs/usbip/vhci_instrument.h"

namespace vadb {
// VirtualADBClient is a companion class for USBForwarder, running on
// Cuttlefish. VirtualADBClient collects list of available USB devices from
// Cuttlefish and makes them available to USB/IP.
//
// Purpose of this class is to connect to USBForwarder and make access to
// remote USB devices possible with help of USB/IP protocol.
class VirtualADBClient {
 public:
  VirtualADBClient(usbip::DevicePool* pool, cvd::SharedFD fd,
                   const std::string& usbip_socket_name);

  virtual ~VirtualADBClient() = default;

  // Query remote server; populate available USB devices.
  bool PopulateRemoteDevices();

  // BeforeSelect is Called right before Select() to populate interesting
  // SharedFDs.
  void BeforeSelect(cvd::SharedFDSet* fd_read) const;

  // AfterSelect is Called right after Select() to detect and respond to changes
  // on affected SharedFDs.
  // Return value indicates whether this client is still valid.
  bool AfterSelect(const cvd::SharedFDSet& fd_read);

 private:
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

  // Send new heartbeat request and arm the heartbeat timer.
  bool SendHeartbeat();

  // Heartbeat handler receives response to heartbeat request.
  // Supplied argument indicates, whether remote server is ready to export USB
  // gadget.
  void HandleHeartbeat(bool is_ready);

  // Heartbeat timeout detects situation where heartbeat did not receive
  // matching response. This could be a direct result of device reset.
  bool HandleHeartbeatTimeout();

  // ExecuteCommand creates command header and executes supplied USBCommand.
  // If execution was successful, command will be stored internally until
  // response arrives.
  bool ExecuteCommand(std::unique_ptr<USBCommand> cmd);

  usbip::DevicePool* pool_;
  cvd::SharedFD fd_;
  cvd::SharedFD timer_;
  usbip::VHCIInstrument vhci_;
  bool is_remote_server_ready_ = false;

  uint32_t tag_ = 0;
  // Assign an 'invalid' tag as previously sent heartbeat command. This will
  // prevent heartbeat timeout handler from finding a command if none was sent.
  uint32_t heartbeat_tag_ = ~0;
  std::map<uint32_t, std::unique_ptr<USBCommand>> commands_;

  VirtualADBClient(const VirtualADBClient& other) = delete;
  VirtualADBClient& operator=(const VirtualADBClient& other) = delete;
};

}  // namespace vadb
