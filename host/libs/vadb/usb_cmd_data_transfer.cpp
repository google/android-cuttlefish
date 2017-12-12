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

#include "host/libs/vadb/usb_cmd_data_transfer.h"

namespace vadb {
USBCmdDataTransfer::USBCmdDataTransfer(
    uint8_t bus_id, uint8_t dev_id, uint8_t endpoint, bool is_host_to_device,
    uint32_t deadline, std::vector<uint8_t> data,
    usbip::Device::AsyncTransferReadyCB callback)
    : data_(std::move(data)), callback_(std::move(callback)) {
  req_.bus_id = bus_id;
  req_.dev_id = dev_id;
  req_.endpoint_id = endpoint;
  req_.is_host_to_device = is_host_to_device;
  req_.length = data_.size();
  req_.timeout = deadline;
}

bool USBCmdDataTransfer::OnRequest(const cvd::SharedFD& fd) {
  if (fd->Write(&req_, sizeof(req_)) != sizeof(req_)) {
    LOG(ERROR) << "Short write: " << fd->StrError();
    return false;
  }

  if (req_.is_host_to_device && data_.size() > 0) {
    if (fd->Write(data_.data(), data_.size()) != data_.size()) {
      LOG(ERROR) << "Short write: " << fd->StrError();
      return false;
    }
  }

  return true;
}

bool USBCmdDataTransfer::OnResponse(bool is_success, const cvd::SharedFD& fd) {
  if (!is_success) {
    callback_(false, std::move(data_));
    return true;
  }

  if (!req_.is_host_to_device) {
    int32_t len;
    if (fd->Read(&len, sizeof(len)) != sizeof(len)) {
      LOG(ERROR) << "Short read: " << fd->StrError();
      callback_(false, std::move(data_));
      return false;
    }

    if (len > 0) {
      data_.resize(len);
      int32_t got = 0;
      // Virtio sends data in 32k packets. We may have to do a few reads.
      while (got < len) {
        auto packetsize = fd->Read(&data_[got], len - got);
        got += packetsize;

        if (fd->GetErrno() != 0) {
          // This could, technically, also be a disconnect.
          LOG(ERROR) << "Read failed: " << fd->StrError();
          return false;
        } else if (packetsize == 0) {
          LOG(ERROR) << "Short read; remote end disconnected.";
          return false;
        }
      }
    }
  }

  callback_(true, std::move(data_));
  return true;
}
}  // namespace vadb
