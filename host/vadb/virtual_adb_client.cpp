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
#include <algorithm>
#include <memory>
#include "common/libs/fs/shared_select.h"
#include "host/vadb/usb_cmd_attach.h"
#include "host/vadb/usb_cmd_control_transfer.h"
#include "host/vadb/usb_cmd_data_transfer.h"
#include "host/vadb/usb_cmd_device_list.h"
#include "host/vadb/virtual_adb_client.h"

namespace vadb {

void VirtualADBClient::RegisterDevice(
    const usb_forward::DeviceInfo& dev,
    const std::vector<usb_forward::InterfaceInfo>& ifaces) {
  auto d = std::unique_ptr<usbip::Device>(new usbip::Device);
  d->vendor_id = dev.vendor_id;
  d->product_id = dev.product_id;
  d->dev_version = dev.dev_version;
  d->dev_class = dev.dev_class;
  d->dev_subclass = dev.dev_subclass;
  d->dev_protocol = dev.dev_protocol;
  d->speed = dev.speed;
  d->configurations_count = dev.num_configurations;
  d->configuration_number = dev.cur_configuration;

  for (const auto& iface : ifaces) {
    d->interfaces.push_back(usbip::Device::Interface{
        iface.if_class, iface.if_subclass, iface.if_protocol});
  }

  uint8_t bus_id = dev.bus_id;
  uint8_t dev_id = dev.dev_id;

  d->handle_attach = [this, bus_id, dev_id]() -> bool {
    return HandleAttach(bus_id, dev_id);
  };

  d->handle_control_transfer =
      [this, bus_id, dev_id](
          const usbip::CmdRequest& r, uint32_t deadline,
          std::vector<uint8_t> data,
          usbip::Device::AsyncTransferReadyCB callback) -> bool {
    return HandleDeviceControlRequest(bus_id, dev_id, r, deadline,
                                      std::move(data), std::move(callback));
  };

  d->handle_data_transfer =
      [this, bus_id, dev_id](
          uint8_t endpoint, bool is_host_to_device, uint32_t deadline,
          std::vector<uint8_t> data,
          usbip::Device::AsyncTransferReadyCB callback) -> bool {
    return HandleDeviceDataRequest(bus_id, dev_id, endpoint, is_host_to_device,
                                   deadline, std::move(data),
                                   std::move(callback));
  };

  pool_->AddDevice(usbip::DevicePool::BusDevNumber{bus_id, dev_id},
                   std::move(d));

  // Attach this device.
  HandleAttach(bus_id, dev_id);
}

bool VirtualADBClient::PopulateRemoteDevices() {
  return ExecuteCommand(std::unique_ptr<USBCommand>(new USBCmdDeviceList(
      [this](const usb_forward::DeviceInfo& info,
             const std::vector<usb_forward::InterfaceInfo>& ifaces) {
        RegisterDevice(info, ifaces);
      })));
}

bool VirtualADBClient::HandleDeviceControlRequest(
    uint8_t bus_id, uint8_t dev_id, const usbip::CmdRequest& r,
    uint32_t timeout, std::vector<uint8_t> data,
    usbip::Device::AsyncTransferReadyCB callback) {
  return ExecuteCommand(std::unique_ptr<USBCommand>(new USBCmdControlTransfer(
      bus_id, dev_id, r.type, r.cmd, r.value, r.index, timeout, std::move(data),
      std::move(callback))));
}

bool VirtualADBClient::HandleDeviceDataRequest(
    uint8_t bus_id, uint8_t dev_id, uint8_t endpoint, bool is_host_to_device,
    uint32_t deadline, std::vector<uint8_t> data,
    usbip::Device::AsyncTransferReadyCB callback) {
  return ExecuteCommand(std::unique_ptr<USBCommand>(
      new USBCmdDataTransfer(bus_id, dev_id, endpoint, is_host_to_device,
                             deadline, std::move(data), std::move(callback))));
}

bool VirtualADBClient::HandleAttach(uint8_t bus_id, uint8_t dev_id) {
  return ExecuteCommand(
      std::unique_ptr<USBCommand>(new USBCmdAttach(bus_id, dev_id)));
}

bool VirtualADBClient::ExecuteCommand(std::unique_ptr<USBCommand> cmd) {
  uint32_t this_tag = tag_;
  tag_++;
  usb_forward::RequestHeader hdr{cmd->Command(), this_tag};
  if (fd_->Write(&hdr, sizeof(hdr)) != sizeof(hdr)) {
    LOG(ERROR) << "Could not contact USB Forwarder: " << fd_->StrError();
    return false;
  }

  if (!cmd->OnRequest(fd_)) return false;

  commands_[this_tag] = std::move(cmd);
  return true;
}

// BeforeSelect is Called right before Select() to populate interesting
// SharedFDs.
void VirtualADBClient::BeforeSelect(avd::SharedFDSet* fd_read) const {
  fd_read->Set(fd_);
}

// AfterSelect is Called right after Select() to detect and respond to changes
// on affected SharedFDs.
// Return value indicates whether this client is still valid.
bool VirtualADBClient::AfterSelect(const avd::SharedFDSet& fd_read) {
  if (fd_read.IsSet(fd_)) {
    usb_forward::ResponseHeader rhdr;
    if (fd_->Read(&rhdr, sizeof(rhdr)) != sizeof(rhdr)) {
      LOG(ERROR) << "Could not read from USB Forwarder: " << fd_->StrError();
      // TODO(ender): it is very likely the connection has been dropped by QEmu.
      // Should we cancel all pending commands now?
      return false;
    }

    auto iter = commands_.find(rhdr.tag);
    if (iter == commands_.end()) {
      LOG(ERROR)
          << "Response does not match any of the previously queued commands!";
      // TODO(ender): This looks like a protocol error. What should happen now?
      // Should we cancel all pending commands now?
      return false;
    }

    iter->second->OnResponse(rhdr.status == usb_forward::StatusSuccess, fd_);
    commands_.erase(iter);
  }

  return true;
}

}  // namespace vadb
