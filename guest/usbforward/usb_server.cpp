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

#include "guest/usbforward/usb_server.h"

#include <string>
#include <vector>
#include <strings.h>
#include <cutils/log.h>
#include <libusb/libusb.h>
#include "common/libs/fs/shared_select.h"
#include "guest/usbforward/protocol.h"

namespace {
void GetDeviceInfo(libusb_device* dev, DeviceInfo* info,
                   std::vector<InterfaceInfo>* ifaces) {
  libusb_device_descriptor desc;
  libusb_config_descriptor* conf;

  memset(info, 0, sizeof(*info));

  int res = libusb_get_device_descriptor(dev, &desc);
  if (res < 0) {
    // This shouldn't really happen.
    ALOGE("libusb_get_device_descriptor failed %d", res);
    return;
  }

  res = libusb_get_active_config_descriptor(dev, &conf);
  if (res < 0) {
    // This shouldn't really happen.
    ALOGE("libusb_get_active_config_descriptor failed %d", res);
    libusb_free_config_descriptor(conf);
    return;
  }

  info->vendor_id = desc.idVendor;
  info->product_id = desc.idProduct;
  info->dev_version = desc.bcdDevice;
  info->dev_class = desc.bDeviceClass;
  info->dev_subclass = desc.bDeviceSubClass;
  info->dev_protocol = desc.bDeviceProtocol;
  info->speed = libusb_get_device_speed(dev);
  info->num_configurations = desc.bNumConfigurations;
  info->num_interfaces = conf->bNumInterfaces;
  info->cur_configuration = conf->bConfigurationValue;
  info->bus_id = libusb_get_bus_number(dev);
  info->dev_id = libusb_get_device_address(dev);

  if (ifaces != nullptr) {
    for (int ifidx = 0; ifidx < conf->bNumInterfaces; ++ifidx) {
      const libusb_interface& iface = conf->interface[ifidx];
      for (int altidx = 0; altidx < iface.num_altsetting; ++altidx) {
        const libusb_interface_descriptor& alt = iface.altsetting[altidx];
        ifaces->push_back(InterfaceInfo{alt.bInterfaceClass,
                                        alt.bInterfaceSubClass,
                                        alt.bInterfaceProtocol, 0});
      }
    }
  }
  libusb_free_config_descriptor(conf);
}

uint16_t MakeDeviceKey(uint8_t bus_id, uint8_t dev_id) {
  return bus_id << 8 | dev_id;
}
}  // anonymous namespace

USBServer::~USBServer() {
  for (const auto& dev : attached_devices_) {
    libusb_close(dev.second);
  }

  for (const auto& dev : devices_) {
    libusb_unref_device(dev.second);
  }
}

bool USBServer::Init() {
  auto res = libusb_init(nullptr);
  if (res < 0) {
    // res is LIBUSB_ERROR in this context.
    ALOGE("libusb_init failed %d", res);
    return false;
  }

  libusb_device** devices;
  int32_t cnt = libusb_get_device_list(nullptr, &devices);
  if (cnt <= 0) {
    ALOGE("libusb_get_device_list failed %d", cnt);
    return false;
  }

  for (int index = 0; index < cnt; index++) {
    auto key = MakeDeviceKey(libusb_get_bus_number(devices[index]),
                             libusb_get_device_address(devices[index]));

    libusb_ref_device(devices[index]);
    devices_[key] = devices[index];
  }

  return true;
}

void USBServer::HandleDeviceList() {
  // Iterate all devices and send structure for every found device.
  DeviceInfo info;
  int32_t size = devices_.size();

  // Write header: number of devices.
  fd_->Write(&size, sizeof(size));

  for (const auto& dev : devices_) {
    std::vector<InterfaceInfo> ifaces;
    GetDeviceInfo(dev.second, &info, &ifaces);
    fd_->Write(&info, sizeof(info));
    fd_->Write(ifaces.data(), ifaces.size() * sizeof(InterfaceInfo));
  }
}

void USBServer::HandleAttach() {
  AttachRequest req;
  // If disconnected prematurely, don't send response.
  if (fd_->Read(&req, sizeof(req)) != sizeof(req)) return;
  // To simplify our lives, let's use status similar to USB/IP.
  int32_t status = 1;

  // Force nul-terminate path.
  const auto key = MakeDeviceKey(req.bus_id, req.dev_id);
  auto iter = devices_.find(key);
  if (iter == devices_.end()) {
    ALOGE("No device found for %x-%x", req.bus_id, req.dev_id);
    fd_->Write(&status, sizeof(status));
    return;
  }

  libusb_device_handle* handle;
  auto res = libusb_open(iter->second, &handle);
  if (res < 0) {
    ALOGE("libusb_open failed %d", res);
    fd_->Write(&status, sizeof(status));
    return;
  }

  if (handle) attached_devices_[key] = handle;
  // Indicate failure if we don't have the handle.
  status = (handle == nullptr);
  fd_->Write(&status, sizeof(status));
}

void USBServer::HandleControlTransfer() {
  ControlTransfer req;
  // If disconnected prematurely, don't send response.
  if (fd_->Read(&req, sizeof(req)) != sizeof(req)) return;

  std::vector<uint8_t> data;
  data.resize(req.length);

  bool req_out = ((req.type & 0x80) == 0);

  if (req_out && req.length) {
    // If disconnected prematurely, don't send response.
    if (fd_->Read(data.data(), req.length) != req.length) return;
  }

  int32_t status = 1;
  int32_t len = 0;
  auto handle_iter =
      attached_devices_.find(MakeDeviceKey(req.bus_id, req.dev_id));

  if (handle_iter != attached_devices_.end()) {
    // Now that we read the whole request and we have a previously attached
    // device, execute control transfer.
    len = libusb_control_transfer(handle_iter->second, req.type, req.cmd,
                                  req.value, req.index, data.data(), req.length,
                                  req.timeout);

    status = (len < 0);
    if (status) {
      ALOGE("USB request failed %d", len);
    }
  }

  fd_->Write(&status, sizeof(status));

  if (!status && !req_out) {
    fd_->Write(&len, sizeof(len));
    if (len > 0) {
      fd_->Write(data.data(), len);
    }
  }
}

void USBServer::HandleDataTransfer() {
  DataTransfer req;
  // If disconnected prematurely, don't send response.
  if (fd_->Read(&req, sizeof(req)) != sizeof(req)) return;

  std::vector<uint8_t> data;
  data.resize(req.length);

  if (req.is_host_to_device && req.length) {
    // If disconnected prematurely, don't send response.
    if (fd_->Read(data.data(), req.length) != req.length) return;
  }

  int32_t status = 1;
  int32_t len = 0;
  auto handle_iter =
      attached_devices_.find(MakeDeviceKey(req.bus_id, req.dev_id));

  if (handle_iter != attached_devices_.end()) {
    // Now that we read the whole request and we know device was previously
    // attached we are good to execute data transfer.

    // Claim and release default interface for the duration of a transfer.
    libusb_claim_interface(handle_iter->second, 0);

    int ret_len = 0;
    ALOGI("Requesting %d bytes of data from EP %d with TO %d", req.length,
          req.endpoint_id, req.timeout);

    // TODO(ender): Remove the timeout modification below when read requests
    // finally complete.
    // As of now, USB driver seems to be blocking on read requests.
    auto err = libusb_bulk_transfer(
        handle_iter->second,
        req.endpoint_id |
            (req.is_host_to_device ? LIBUSB_ENDPOINT_OUT : LIBUSB_ENDPOINT_IN),
        data.data(), req.length, &ret_len, req.timeout + 1000);
    libusb_release_interface(handle_iter->second, 0);

    status = (err != 0);
    if (status) {
      ALOGE("USB request failed %d, %d", err, errno);
    } else {
      ALOGI("Bulk transfer request result: %d / %d", err, ret_len);
      len = ret_len;
    }
  }

  fd_->Write(&status, sizeof(status));

  if (!status && !req.is_host_to_device) {
    fd_->Write(&len, sizeof(len));
    if (len > 0) {
      fd_->Write(data.data(), len);
    }
  }
}

void USBServer::Serve() {
  avd::SharedFDSet rset;
  while (true) {
    rset.Zero();
    rset.Set(fd_);
    int ret = avd::Select(&rset, nullptr, nullptr, nullptr);

    if (ret < 0) continue;

    if (rset.IsSet(fd_)) {
      uint32_t cmd;
      char data;
      if (fd_->Read(&cmd, sizeof(cmd)) < int(sizeof(cmd))) {
        ALOGE("Could not read data from input stream: %s", fd_->StrError());
        continue;
      }

      switch (cmd) {
        case CmdDeviceList:
          HandleDeviceList();
          break;

        case CmdAttach:
          HandleAttach();

        case CmdControlTransfer:
          HandleControlTransfer();
          break;

        case CmdDataTransfer:
          HandleDataTransfer();
          break;

        default:
          ALOGE("Discarding unknown command %08x", cmd);
      }
    }
  }
}
