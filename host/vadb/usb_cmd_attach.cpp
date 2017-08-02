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

#include "guest/usbforward/protocol.h"
#include "host/vadb/usb_cmd_attach.h"

namespace vadb {
bool USBCmdAttach::OnRequest(const avd::SharedFD& fd) {
  if (fd->Write(&req_, sizeof(req_)) != sizeof(req_)) {
    LOG(ERROR) << "Short write: " << fd->StrError();
    return false;
  }
  return true;
}

bool USBCmdAttach::OnResponse(bool is_success, const avd::SharedFD& data) {
  if (!is_success) return false;
  LOG(INFO) << "Attach successful.";
  return true;
}

USBCmdAttach::USBCmdAttach(uint8_t bus_id, uint8_t dev_id) {
  req_.bus_id = bus_id;
  req_.dev_id = dev_id;
}
}  // namespace vadb