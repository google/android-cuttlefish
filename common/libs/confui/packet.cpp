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

#include "common/libs/confui/packet.h"

#include <algorithm>
#include <iostream>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/confui/protocol.h"
#include "common/libs/confui/utils.h"
#include "common/libs/fs/shared_buf.h"

namespace cuttlefish {
namespace confui {
namespace packet {
ConfUiMessage PayloadToConfUiMessage(const std::string& str_to_parse) {
  auto tokens = android::base::Split(str_to_parse, ":");
  ConfUiCheck(tokens.size() >= 3)
      << "PayloadToConfUiMessage takes \"" + str_to_parse + "\""
      << "and does not have 3 tokens";
  std::string msg;
  std::for_each(tokens.begin() + 2, tokens.end() - 1,
                [&msg](auto& token) { msg.append(token + ":"); });
  msg.append(*tokens.rbegin());
  return {tokens[0], tokens[1], msg};
}

// Use only this function to make a packet to send over the confirmation
// ui packet layer
template <typename... Args>
static Payload ToPayload(const ConfUiCmd cmd, const std::string& session_id,
                         Args&&... args) {
  std::string cmd_str = ToString(cmd);
  std::string msg =
      ArgsToString(session_id, ":", cmd_str, ":", std::forward<Args>(args)...);
  PayloadHeader header;
  header.payload_length_ = msg.size();
  return {header, msg};
}

template <typename... Args>
static bool WritePayload(SharedFD d, const ConfUiCmd cmd,
                         const std::string& session_id, Args&&... args) {
  if (!d->IsOpen()) {
    LOG(ERROR) << "file, socket, etc, is not open to write";
    return false;
  }
  auto [payload, msg] = ToPayload(cmd, session_id, std::forward<Args>(args)...);

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

static std::optional<ConfUiMessage> ReadPayload(SharedFD s) {
  if (!s->IsOpen()) {
    LOG(ERROR) << "file, socket, etc, is not open to read";
    return std::nullopt;
  }
  PayloadHeader p;
  auto nread = ReadExactBinary(s, &p);

  if (nread != sizeof(p)) {
    return std::nullopt;
  }

  if (p.payload_length_ == 0) {
    return {{SESSION_ANY, ToString(ConfUiCmd::kUnknown), std::string{""}}};
  }

  if (p.payload_length_ >= kMaxPayloadLength) {
    LOG(ERROR) << "Payload length must be less than " << kMaxPayloadLength;
    return std::nullopt;
  }

  std::unique_ptr<char[]> buf{new char[p.payload_length_ + 1]};
  nread = ReadExact(s, buf.get(), p.payload_length_);
  buf[p.payload_length_] = 0;
  if (nread != p.payload_length_) {
    return std::nullopt;
  }
  std::string msg_to_parse{buf.get()};
  auto [session_id, type, contents] = PayloadToConfUiMessage(msg_to_parse);
  return {{session_id, type, contents}};
}

std::optional<ConfUiMessage> RecvConfUiMsg(SharedFD fd) {
  return ReadPayload(fd);
}

bool SendCmd(SharedFD fd, const std::string& session_id, ConfUiCmd cmd,
             const std::string& additional_info) {
  return WritePayload(fd, cmd, session_id, additional_info);
}

bool SendAck(SharedFD fd, const std::string& session_id, const bool is_success,
             const std::string& additional_info) {
  return WritePayload(fd, ConfUiCmd::kCliAck, session_id,
                      ToCliAckMessage(is_success, additional_info));
}

bool SendResponse(SharedFD fd, const std::string& session_id,
                  const std::string& additional_info) {
  return WritePayload(fd, ConfUiCmd::kCliRespond, session_id, additional_info);
}

}  // end of namespace packet
}  // end of namespace confui
}  // end of namespace cuttlefish
