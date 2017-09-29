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
#include "common/libs/usbforward/protocol.h"

namespace vadb {
// USBCommand is an abstraction of a proxied USB command.
// Instances of this object all share the following life cycle:
// 1) A specific instance (COMMAND) is being created.
// 2) Instance owner (OWNER) sends RequestHeader.
// 3) OWNER calls COMMAND.OnRequest() to send any relevant, additional
//    information.
// 4) OWNER queues COMMAND until response arrives.
//
// At this point instance owner can process next command in queue. Then,
// eventually:
//
// 5) OWNER receives matching ResponseHeader.
// 6) OWNER calls COMMAND.OnResponse(), supplying FD that carries additional
//    data.
// 7) OWNER dequeues and deletes COMMAND.
class USBCommand {
 public:
  USBCommand() = default;
  virtual ~USBCommand() = default;

  // Command returns a specific usbforward command ID associated with this
  // request.
  virtual usb_forward::Command Command() = 0;

  // OnRequest is called whenever additional data relevant to this command
  // (other than RequestHeader) should be sent.
  // Returns false, if communication with remote host failed (and should be
  // terminated).
  virtual bool OnRequest(const avd::SharedFD& data) = 0;

  // OnResponse is called whenever additional data relevant to this command
  // (other than ResponseHeader) should be received.
  // Returns false, if communication with remote host failed (and should be
  // terminated).
  virtual bool OnResponse(bool is_success, const avd::SharedFD& data) = 0;

 private:
  USBCommand(const USBCommand& other) = delete;
  USBCommand& operator=(const USBCommand& other) = delete;
};

}  // namespace vadb
