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
#include "host/vadb/virtual_adb.h"

namespace vadb {

void VirtualADB::RegisterDevice(
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

  pool_.AddDevice(usbip::DevicePool::BusDevNumber{bus_id, dev_id},
                  std::move(d));
}

bool VirtualADB::PopulateRemoteDevices() {
  return ExecuteCommand(std::unique_ptr<USBCommand>(new USBCmdDeviceList(
      [this](const usb_forward::DeviceInfo& info,
             const std::vector<usb_forward::InterfaceInfo>& ifaces) {
        RegisterDevice(info, ifaces);
      })));
}

bool VirtualADB::HandleDeviceControlRequest(
    uint8_t bus_id, uint8_t dev_id, const usbip::CmdRequest& r,
    uint32_t timeout, std::vector<uint8_t> data,
    usbip::Device::AsyncTransferReadyCB callback) {
  return ExecuteCommand(std::unique_ptr<USBCommand>(new USBCmdControlTransfer(
      bus_id, dev_id, r.type, r.cmd, r.value, r.index, timeout, std::move(data),
      std::move(callback))));
}

bool VirtualADB::HandleDeviceDataRequest(
    uint8_t bus_id, uint8_t dev_id, uint8_t endpoint, bool is_host_to_device,
    uint32_t deadline, std::vector<uint8_t> data,
    usbip::Device::AsyncTransferReadyCB callback) {
  return ExecuteCommand(std::unique_ptr<USBCommand>(
      new USBCmdDataTransfer(bus_id, dev_id, endpoint, is_host_to_device,
                             deadline, std::move(data), std::move(callback))));
}

bool VirtualADB::HandleAttach(uint8_t bus_id, uint8_t dev_id) {
  return ExecuteCommand(
      std::unique_ptr<USBCommand>(new USBCmdAttach(bus_id, dev_id)));
}

bool VirtualADB::ExecuteCommand(std::unique_ptr<USBCommand> cmd) {
  std::lock_guard<std::mutex> guard(commands_mutex_);

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

void VirtualADB::ReceiveThread() {
  avd::SharedFDSet rset;
  while (true) {
    rset.Zero();
    rset.Set(fd_);
    auto res = avd::Select(&rset, nullptr, nullptr, nullptr);
    if (res <= 0) continue;

    if (rset.IsSet(fd_)) {
      usb_forward::ResponseHeader rhdr;
      if (fd_->Read(&rhdr, sizeof(rhdr)) != sizeof(rhdr)) {
        LOG(ERROR) << "Could not read from USB Forwarder: " << fd_->StrError();
        // TODO(ender): this is likely an indication that the remote end has
        // rebooted. We should probably just fail all USB/IP commands here.
        continue;
      }

      std::lock_guard<std::mutex> lock(commands_mutex_);
      auto iter = commands_.find(rhdr.tag);
      if (iter == commands_.end()) {
        LOG(ERROR)
            << "Response does not match any of the previously queued commands!";
        // TODO(ender): Doesn't make much sense to continue. We should just
        // reset stream here by closing and re-opening connection.
        continue;
      }

      iter->second->OnResponse(rhdr.status == usb_forward::StatusSuccess, fd_);
      commands_.erase(iter);
    }
  }
}

bool VirtualADB::Init() {
  fd_ = avd::SharedFD::SocketLocalClient(path_.c_str(), false, SOCK_STREAM);
  if (!fd_->IsOpen()) {
    LOG(ERROR) << "Could not open " << path_ << ": " << fd_->StrError();
    return false;
  }

  receive_thread_.reset(new std::thread([this]() { ReceiveThread(); }));

  while (true) {
    PopulateRemoteDevices();
    if (pool_.Size() > 0) break;
    // Wait for remote to be ready.
    sleep(1);
  }

  // Attach devices immediately.
  for (const auto& dev : pool_) {
    dev.second->handle_attach();
  }

  return true;
}

const usbip::DevicePool& VirtualADB::Pool() const { return pool_; }

}  // namespace vadb