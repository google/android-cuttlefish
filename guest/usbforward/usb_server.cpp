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
#define NDEBUG

#include "guest/usbforward/usb_server.h"

#include <string>
#include <vector>
#include <strings.h>
#include <cutils/log.h>
#include <libusb/libusb.h>
#include "common/libs/fs/shared_select.h"
#include "guest/usbforward/protocol.h"

namespace {
// USBServer exports device kExportedVendorID:kExportedProductID to the server.
// We will not support exporting multiple USB devices as there's no practical
// need for this.
constexpr uint16_t kExportedVendorID = 0x18d1;
constexpr uint16_t kExportedProductID = 0x4ee7;

// Use default BUS and DEVICE IDs so that it's easier to attach over USB/IP.
constexpr uint8_t kDefaultBusID = 1;
constexpr uint8_t kDefaultDevID = 1;

std::unique_ptr<libusb_device_handle, void (*)(libusb_device_handle*)>
GetDevice() {
  std::unique_ptr<libusb_device_handle, void (*)(libusb_device_handle*)> res(
      libusb_open_device_with_vid_pid(nullptr, kExportedVendorID,
                                      kExportedProductID),
      [](libusb_device_handle* h) {
        libusb_release_interface(h, 0);
        libusb_close(h);
      });

  if (res) libusb_claim_interface(res.get(), 0);

  return res;
}

bool GetDeviceInfo(DeviceInfo* info, std::vector<InterfaceInfo>* ifaces) {
  auto handle = GetDevice();
  if (!handle) return false;

  // This function does not modify the reference count of the returned device,
  // so do not feel compelled to unreference it when you are done.
  libusb_device* dev = libusb_get_device(handle.get());

  libusb_device_descriptor desc;
  libusb_config_descriptor* conf;
  memset(info, 0, sizeof(*info));

  int res = libusb_get_device_descriptor(dev, &desc);
  if (res < 0) {
    // This shouldn't really happen.
    ALOGE("libusb_get_device_descriptor failed %d", res);
    return false;
  }

  res = libusb_get_active_config_descriptor(dev, &conf);
  if (res < 0) {
    // This shouldn't really happen.
    ALOGE("libusb_get_active_config_descriptor failed %d", res);
    libusb_free_config_descriptor(conf);
    return false;
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
  info->bus_id = kDefaultBusID;
  info->dev_id = kDefaultDevID;

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
  return true;
}
}  // anonymous namespace

void USBServer::HandleDeviceList() {
  // Iterate all devices and send structure for every found device.
  // Write header: number of devices.
  DeviceInfo info;
  std::vector<InterfaceInfo> ifaces;
  if (GetDeviceInfo(&info, &ifaces)) {
    uint32_t cnt = 1;
    fd_->Write(&cnt, sizeof(cnt));
    fd_->Write(&info, sizeof(info));
    fd_->Write(ifaces.data(), ifaces.size() * sizeof(InterfaceInfo));
  } else {
    // No devices.
    uint32_t cnt = 0;
    fd_->Write(&cnt, sizeof(cnt));
  }
}

void USBServer::HandleAttach() {
  handle_ = GetDevice();
  uint32_t status = !handle_;
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

  if (handle_) {
    // Now that we read the whole request and we have a previously attached
    // device, execute control transfer.
    len = libusb_control_transfer(handle_.get(), req.type, req.cmd, req.value,
                                  req.index, data.data(), req.length,
                                  req.timeout);

    status = (len < 0);
    if (status) {
      ALOGE("USB request failed %d", len);
    }
  } else {
    ALOGE("USB Device not attached.");
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

  if (handle_) {
    // Now that we read the whole request and we know device was previously
    // attached we are good to execute data transfer.

    int ret_len = 0;
    ALOGV("Requesting %d bytes of data %s EP %d with TO %d", req.length,
          (req.is_host_to_device ? "to" : "from"), req.endpoint_id,
          req.timeout);

    // TODO(ender): Remove the timeout modification below and make the
    // communication fully asynchronous. As of now, USB driver seems to be
    // blocking on read requests, because there's multiple requests being
    // executed, and some of them are blocking by design. Sequential nature of
    // this server makes it currently impossible to handle streaming requests
    // while blocking requests wait patiently for response.
    auto err = libusb_bulk_transfer(
        handle_.get(),
        req.endpoint_id |
            (req.is_host_to_device ? LIBUSB_ENDPOINT_OUT : LIBUSB_ENDPOINT_IN),
        data.data(), req.length, &ret_len, req.timeout + 100);
    status = (err != 0);
    len = ret_len;
    if (status) {
      ALOGV("USB request failed %d, %d", err, errno);
    }
  } else {
    ALOGE("USB Device not attached.");
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
        // There's nobody on the other side.
        sleep(3);
        continue;
      }

      switch (cmd) {
        case CmdDeviceList:
          ALOGV("Processing DeviceList command");
          HandleDeviceList();
          break;

        case CmdAttach:
          ALOGV("Processing Attach command");
          HandleAttach();

        case CmdControlTransfer:
          ALOGV("Processing ControlTransfer command");
          HandleControlTransfer();
          break;

        case CmdDataTransfer:
          ALOGV("Processing DataTransfer command");
          HandleDataTransfer();
          break;

        default:
          ALOGE("Discarding unknown command %08x", cmd);
      }
    }
  }
}
