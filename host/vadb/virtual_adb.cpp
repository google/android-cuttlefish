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
#include "host/vadb/virtual_adb.h"

namespace vadb {

void VirtualADB::RegisterDevice(const DeviceInfo& dev,
                                const std::vector<InterfaceInfo>& ifaces) {
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

  d->handle_request = [this, bus_id, dev_id](
                          const usbip::CmdRequest& r,
                          const std::vector<uint8_t>& in,
                          std::vector<uint8_t>* out) -> bool {
    return HandleDeviceControlRequest(bus_id, dev_id, r, in, out);
  };

  pool_.AddDevice(usbip::DevicePool::BusDevNumber{bus_id, dev_id},
                  std::move(d));
}

bool VirtualADB::PopulateRemoteDevices() {
  uint32_t cmd = CmdDeviceList;
  if (fd_->Write(&cmd, sizeof(cmd)) != sizeof(cmd)) {
    LOG(ERROR) << "Could not contact USB Forwarder: " << fd_->StrError();
    return false;
  }

  int32_t count;
  if (fd_->Read(&count, sizeof(count)) != sizeof(count)) {
    LOG(ERROR) << "Short read: " << fd_->StrError();
    return 1;
  }

  while (count-- > 0) {
    DeviceInfo dev;
    std::vector<InterfaceInfo> ifaces;

    if (fd_->Read(&dev, sizeof(dev)) != sizeof(dev)) {
      LOG(ERROR) << "Short read: " << fd_->StrError();
      return 1;
    }

    ifaces.resize(dev.num_interfaces);
    if (fd_->Read(ifaces.data(), ifaces.size() * sizeof(InterfaceInfo)) !=
        ifaces.size() * sizeof(InterfaceInfo)) {
      LOG(ERROR) << "Short read: " << fd_->StrError();
      return 1;
    }

    LOG(INFO) << "Found remote device 0x" << std::hex << dev.vendor_id << ":"
              << dev.product_id;
    RegisterDevice(dev, ifaces);
  }
  return true;
}

bool VirtualADB::Init() {
  fd_ = avd::SharedFD::SocketLocalClient(path_.c_str(), false, SOCK_STREAM);
  if (!fd_->IsOpen()) {
    LOG(ERROR) << "Could not open " << path_ << ": " << fd_->StrError();
    return false;
  }

  return PopulateRemoteDevices();
}

bool VirtualADB::HandleDeviceControlRequest(
    uint8_t bus_id, uint8_t dev_id, const usbip::CmdRequest& r,
    const std::vector<uint8_t>& data_out, std::vector<uint8_t>* data_in) {
  LOG(INFO) << "Executing command on " << int(bus_id) << "-" << int(dev_id);

  uint32_t cmd = CmdExecute;
  if (fd_->Write(&cmd, sizeof(cmd)) != sizeof(cmd)) {
    LOG(ERROR) << "Could not contact USB Forwarder: " << fd_->StrError();
    return false;
  }

  ExecuteRequest rq;

  rq.bus_id = bus_id;
  rq.dev_id = dev_id;
  rq.type = r.type;
  rq.cmd = r.cmd;
  rq.value = r.value;
  rq.index = r.index;
  rq.length = r.length;
  rq.timeout = 0;

  if (fd_->Write(&rq, sizeof(rq)) != sizeof(rq)) {
    LOG(ERROR) << "Short write: " << fd_->StrError();
    return false;
  }

  if ((rq.type & 0x80) == 0) {
    if (r.length > 0) {
      if (fd_->Write(data_out.data(), r.length) != r.length) {
        LOG(ERROR) << "Short write: " << fd_->StrError();
        return false;
      }
    }
  }

  int32_t status;
  if (fd_->Read(&status, sizeof(status)) != sizeof(status)) {
    LOG(ERROR) << "Short read: " << fd_->StrError();
    return false;
  }

  if (status == 0) {
    if (rq.type & 0x80) {
      int32_t len;
      if (fd_->Read(&len, sizeof(len)) != sizeof(len)) {
        LOG(ERROR) << "Short read: " << fd_->StrError();
        return false;
      }

      LOG(INFO) << "Reading payload (" << len << " bytes)";

      if (len > 0) {
        data_in->resize(len);
        if (fd_->Read(data_in->data(), len) != len) {
          LOG(ERROR) << "Short read: " << fd_->StrError();
          return false;
        }
      }
    }
  }

  LOG(INFO) << "Command execution completed with status: " << status;
  return true;
}

bool VirtualADB::HandleAttach(uint8_t bus_id, uint8_t dev_id) {
  LOG(INFO) << "Attaching device " << int(bus_id) << "-" << int(dev_id);

  uint32_t cmd = CmdAttach;
  if (fd_->Write(&cmd, sizeof(cmd)) != sizeof(cmd)) {
    LOG(ERROR) << "Could not contact USB Forwarder: " << fd_->StrError();
    return false;
  }

  AttachRequest rq;
  rq.bus_id = bus_id;
  rq.dev_id = dev_id;

  if (fd_->Write(&rq, sizeof(rq)) != sizeof(rq)) {
    LOG(ERROR) << "Short write: " << fd_->StrError();
    return false;
  }

  int32_t status;
  if (fd_->Read(&status, sizeof(status)) != sizeof(status)) {
    LOG(ERROR) << "Short read: " << fd_->StrError();
    return false;
  }

  LOG(INFO) << "Attach result: " << status;
  return status == 0;
}

const usbip::DevicePool& VirtualADB::Pool() const { return pool_; }

}  // namespace vadb