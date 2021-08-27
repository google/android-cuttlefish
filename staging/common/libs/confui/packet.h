//
// Copyright (C) 2021 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <tuple>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/confui/utils.h"
#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {
namespace confui {
namespace packet {
/*
 * for communication between Confirmation UI guest and host.
 *
 * Payload is actually the header. When we send/recv, besides Payload,
 * the "payload_length_" bytes should be additionally sent/recv'ed.
 *
 * The payload is assumed to be a text (e.g. char[N])
 * The WritePayload will create the string. When read, however,
 * the receiver should parse it
 *
 * The format we use for confirmation UI is:
 *  session_id:type:contents
 *
 * e.g. GooglePay10354:start:my confirmaton message
 */
struct PayloadHeader {
  std::uint32_t payload_length_;
};

// PayloadHeader + the message actually being sent
using Payload = std::tuple<PayloadHeader, std::string>;

// this is for short messages
constexpr const ssize_t kMaxPayloadLength = 1000;

std::optional<std::string> ReadPayload(SharedFD s);

// Use only this function to make a packet to send over the confirmation
// ui packet layer
template <typename... Args>
Payload ToPayload(const std::string& cmd_str, const std::string& session_id,
                  Args&&... args) {
  std::string msg =
      ArgsToString(session_id, ":", cmd_str, ":", std::forward<Args>(args)...);
  PayloadHeader header;
  header.payload_length_ = msg.size();
  return {header, msg};
}

template <typename... Args>
bool WritePayload(SharedFD d, const std::string& cmd_str,
                  const std::string& session_id, Args&&... args) {
  if (!d->IsOpen()) {
    LOG(ERROR) << "file, socket, etc, is not open to write";
    return false;
  }
  auto [payload, msg] =
      ToPayload(cmd_str, session_id, std::forward<Args>(args)...);

  auto nwrite =
      WriteAll(d, reinterpret_cast<const char*>(&payload), sizeof(payload));
  if (nwrite != sizeof(payload)) {
    return false;
  }
  nwrite = cuttlefish::WriteAll(d, msg.c_str(), msg.size());
  if (nwrite != msg.size()) {
    return false;
  }
  return true;
}

}  // end of namespace packet
}  // end of namespace confui
}  // end of namespace cuttlefish
