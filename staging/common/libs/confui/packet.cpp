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

namespace cuttlefish {
namespace confui {
namespace packet {
std::optional<std::string> ReadPayload(SharedFD s) {
  if (!s->IsOpen()) {
    LOG(ERROR) << "file, socket, etc, is not open to read";
    return std::nullopt;
  }
  packet::PayloadHeader p;
  auto nread = ReadExactBinary(s, &p);

  if (nread != sizeof(p)) {
    return std::nullopt;
  }
  if (p.payload_length_ == 0) {
    return {""};
  }

  if (p.payload_length_ >= packet::kMaxPayloadLength) {
    LOG(ERROR) << "Payload length must be less than "
               << packet::kMaxPayloadLength;
    return std::nullopt;
  }

  std::unique_ptr<char[]> buf{new char[p.payload_length_ + 1]};
  nread = ReadExact(s, buf.get(), p.payload_length_);
  buf[p.payload_length_] = 0;
  if (nread != p.payload_length_) {
    ConfUiLog(ERROR) << "The length ReadPayload read does not match.";
    return std::nullopt;
  }
  std::string result{buf.get()};
  return {result};
}
}  // end of namespace packet
}  // end of namespace confui
}  // end of namespace cuttlefish
