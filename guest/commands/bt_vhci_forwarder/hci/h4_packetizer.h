//
// Copyright 2017 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once

#include <functional>
#include <vector>

#include "h4_parser.h"

namespace rootcanal {

// A socket based H4Packetizer. Call OnDataReady whenever
// data can be read from file descriptor fd.
//
// This is only supported on unix.
class H4Packetizer {
 public:
  H4Packetizer(int fd, PacketReadCallback command_cb,
               PacketReadCallback event_cb, PacketReadCallback acl_cb,
               PacketReadCallback sco_cb, PacketReadCallback iso_cb,
               ClientDisconnectCallback disconnect_cb);

  size_t Send(uint8_t type, const uint8_t* data, size_t length);

  void OnDataReady(int fd);

 private:
  int uart_fd_;
  H4Parser h4_parser_;

  ClientDisconnectCallback disconnect_cb_;
  bool disconnected_{false};
};

}  // namespace rootcanal
