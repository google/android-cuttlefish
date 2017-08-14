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
#include "host/vadb/usb_cmd_heartbeat.h"

namespace vadb {
bool USBCmdHeartbeat::OnRequest(const avd::SharedFD& fd) { return true; }

bool USBCmdHeartbeat::OnResponse(bool is_success, const avd::SharedFD& data) {
  callback_(is_success);
  return true;
}

USBCmdHeartbeat::USBCmdHeartbeat(USBCmdHeartbeat::HeartbeatResultCB callback)
    : callback_(callback) {}
}  // namespace vadb
