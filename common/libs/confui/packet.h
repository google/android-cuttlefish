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
#include <functional>
#include <optional>
#include <string>
#include <tuple>

#include "common/libs/confui/protocol.h"
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

// msg will look like "334522:start:Hello I am Here!"
// this function returns 334522, start, "Hello I am Here!"
// if no session id is given, it is regarded as SESSION_ANY
ConfUiMessage PayloadToConfUiMessage(const std::string& str_to_parse);

std::optional<ConfUiMessage> RecvConfUiMsg(SharedFD fd);
bool SendAck(SharedFD fd, const std::string& session_id, const bool is_success,
             const std::string& additional_info);
bool SendResponse(SharedFD fd, const std::string& session_id,
                  const std::string& additional_info);
// for HAL
bool SendCmd(SharedFD fd, const std::string& session_id, ConfUiCmd cmd,
             const std::string& additional_info);

// this is for short messages
constexpr const ssize_t kMaxPayloadLength = 1000;

}  // end of namespace packet
}  // end of namespace confui
}  // end of namespace cuttlefish
