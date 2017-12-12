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
#include "common/libs/fs/shared_select.h"
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

  // BeforeSelect is Called right before Select() to populate interesting
  // SharedFDs.
  void BeforeSelect(avd::SharedFDSet* fd_read) const;

  // AfterSelect is Called right after Select() to detect and respond to changes
  // on affected SharedFDs.
  // Return value indicates whether this client is still valid.
  bool AfterSelect(const avd::SharedFDSet& fd_read);

 private:
  // Respond to message from remote client.
  // Returns false, if client violated protocol or disconnected, indicating,
  // that this instance should no longer be used.
  bool HandleIncomingMessage();

  // Execute command on USB device.
  // Returns false, if connection should be dropped.
  bool HandleSubmitCmd(const CmdHeader& hdr);

  // HandleAsyncDataReady is called asynchronously once previously submitted
  // data transfer (control or bulk) has completed (or failed).
  void HandleAsyncDataReady(uint32_t seq_num, bool is_success,
                            bool is_host_to_device, std::vector<uint8_t> data);

  // Unlink previously submitted message from device queue.
  // Returns false, if connection should be dropped.
  bool HandleUnlinkCmd(const CmdHeader& hdr);

  const DevicePool& pool_;
  avd::SharedFD fd_;

  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;
};

}  // namespace usbip
}  // namespace vadb
