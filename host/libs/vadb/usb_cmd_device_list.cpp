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
#include <glog/logging.h>

#include "host/libs/vadb/usb_cmd_device_list.h"

namespace vadb {
bool USBCmdDeviceList::OnRequest(const cvd::SharedFD& data) {
  LOG(INFO) << "Requesting device list from Cuttlefish...";
  // No action required.
  return true;
}

bool USBCmdDeviceList::OnResponse(bool is_success, const cvd::SharedFD& fd) {
  // This should never happen. If this command fails, something is very wrong.
  if (!is_success) return false;

  int32_t count;
  if (fd->Read(&count, sizeof(count)) != sizeof(count)) {
    LOG(ERROR) << "Short read: " << fd->StrError();
    return false;
  }

  LOG(INFO) << "Device list completed with " << count << " devices.";

  while (count-- > 0) {
    usb_forward::DeviceInfo dev;
    std::vector<usb_forward::InterfaceInfo> ifaces;

    if (fd->Read(&dev, sizeof(dev)) != sizeof(dev)) {
      LOG(ERROR) << "Short read: " << fd->StrError();
      return false;
    }

    ifaces.resize(dev.num_interfaces);
    if (fd->Read(ifaces.data(),
                 ifaces.size() * sizeof(usb_forward::InterfaceInfo)) !=
        ifaces.size() * sizeof(usb_forward::InterfaceInfo)) {
      LOG(ERROR) << "Short read: " << fd->StrError();
      return false;
    }

    LOG(INFO) << "Found remote device 0x" << std::hex << dev.vendor_id << ":"
              << dev.product_id;

    on_device_discovered_(dev, ifaces);
  }

  return true;
}
}  // namespace vadb
