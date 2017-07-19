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

#include "common/libs/fs/shared_fd.h"
#include "host/vadb/usbip/device_pool.h"
#include "host/vadb/usbip/messages.h"

namespace vadb {
namespace usbip {

// Represents USB/IP client, or individual connection to our USB/IP server.
// Multiple clients are allowed, even if practically we anticipate only one
// connection at the time.
class Client final {
 public:
  Client(const DevicePool& pool, const avd::SharedFD& fd)
      : pool_(pool), fd_(fd) {}

  ~Client() {}

  // Respond to message from remote client.
  // Returns false, if client violated protocol or disconnected, indicating,
  // that this instance should no longer be used.
  bool HandleIncomingMessage();

  const avd::SharedFD& fd() const { return fd_; }

 private:
  // Process messages that are valid only while client is detached.
  // Returns false, if conversation was unsuccessful.
  bool HandleOperation();

  // Process messages that are valid only while client is attached.
  // Returns false, if connection should be dropped.
  bool HandleCommand();

  // List remote USB devices.
  // Returns false, if connection should be dropped.
  bool HandleListOp();

  // Attach virtual USB devices to remote host.
  // Returns false, if connection should be dropped.
  bool HandleImportOp();

  // Execute command on USB device.
  // Returns false, if connection should be dropped.
  bool HandleSubmitCmd(const CmdHeader& hdr);

  // Unlink previously submitted message from device queue.
  // Returns false, if connection should be dropped.
  bool HandleUnlinkCmd(const CmdHeader& hdr);

  const DevicePool& pool_;
  avd::SharedFD fd_;

  // True, if client requested USB device attach.
  bool attached_ = false;
  uint16_t proto_version_ = 0;

  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;
};

}  // namespace usbip
}  // namespace vadb
