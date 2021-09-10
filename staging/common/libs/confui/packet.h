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

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/confui/packet_types.h"
#include "common/libs/confui/utils.h"
#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"

/**
 * @file packet.h
 *
 * @brief lowest-level packet for communication between host & guest
 *
 * Each packet has three fields
 *  1. session_id_: the name of the currently active confirmation UI session
 *  2. type_: the type of command/response. E.g. start, stop, ack, abort, etc
 *  3. additional_info_: all the other additional information
 *
 * The binary represenation of each packet is as follows:
 *  n:L[1]:L[2]:...:L[n]:data[1]data[2]data[3]...data[n]
 *
 * The additional_info_ is in general a variable number of items, each
 * is either a byte vector (e.g. std::vector<uint8_t>) or a string.
 *
 * n is the number of items. L[i] is the length of i th item. data[i]
 * is the binary representation of the i th item
 *
 */
namespace cuttlefish {
namespace confui {
namespace packet {

/*
 * methods in namespace impl is not intended for public use
 *
 * For exposed APIs, skip to "start of public APIs
 * or, skip the namespace impl
 */
namespace impl {
template <typename Buffer, typename... Args>
void AppendToBuffer(Buffer& buffer, Args&&... args) {
  (buffer.insert(buffer.end(), std::begin(std::forward<Args>(args)),
                 std::end(std::forward<Args>(args))),
   ...);
}

template <typename... Args>
std::vector<int> MakeSizeHeader(Args&&... args) {
  std::vector<int> lengths;
  (lengths.push_back(std::distance(std::begin(args), std::end(args))), ...);
  return lengths;
}

// Use only this function to make a packet to send over the confirmation
// ui packet layer
template <typename... Args>
Payload ToPayload(const std::string& cmd_str, const std::string& session_id,
                  Args&&... args) {
  using namespace cuttlefish::confui::packet::impl;
  constexpr auto n_args = sizeof...(Args);
  std::stringstream ss;
  ss << ArgsToString(session_id, ":", cmd_str, ":", n_args, ":");
  // create size header
  std::vector<int> size_info =
      impl::MakeSizeHeader(std::forward<Args>(args)...);
  for (const auto sz : size_info) {
    ss << sz << ":";
  }
  std::string header = ss.str();
  std::vector<std::uint8_t> payload_buffer{header.begin(), header.end()};
  impl::AppendToBuffer(payload_buffer, std::forward<Args>(args)...);

  PayloadHeader ph;
  ph.payload_length_ = payload_buffer.size();
  return {ph, payload_buffer};
}
}  // namespace impl

/*
 * start of public methods
 */
std::optional<ParsedPacket> ReadPayload(SharedFD s);

template <typename... Args>
bool WritePayload(SharedFD d, const std::string& cmd_str,
                  const std::string& session_id, Args&&... args) {
  // TODO(kwstephenkim): type check Args... so that they are either
  // kind of std::string or std::vector<1 byte>
  if (!d->IsOpen()) {
    ConfUiLog(ERROR) << "file, socket, etc, is not open to write";
    return false;
  }
  auto [payload_header, data_to_send] =
      impl::ToPayload(cmd_str, session_id, std::forward<Args>(args)...);
  const std::string data_in_str(data_to_send.cbegin(), data_to_send.cend());

  auto nwrite = WriteAll(d, reinterpret_cast<const char*>(&payload_header),
                         sizeof(payload_header));
  if (nwrite != sizeof(payload_header)) {
    return false;
  }
  nwrite = WriteAll(d, reinterpret_cast<const char*>(data_to_send.data()),
                    data_to_send.size());
  if (nwrite != data_to_send.size()) {
    return false;
  }
  return true;
}

}  // end of namespace packet
}  // end of namespace confui
}  // end of namespace cuttlefish
