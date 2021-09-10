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

#include "common/libs/confui/packet_types.h"

#include <sstream>

namespace cuttlefish {
namespace confui {
namespace packet {
std::string ToString(const ParsedPacket& packet) {
  std::stringstream ss;
  ss << "[" << packet.session_id_ << "," << packet.type_ << ",";
  for (auto const& vec : packet.additional_info_) {
    if (vec.empty()) {
      ss << ",";
      continue;
    }
    std::string token(vec.cbegin(), vec.cend());
    ss << token << ",";
  }
  std::string result = ss.str();
  bool is_remove_one_comma = (!packet.additional_info_.empty());
  if (is_remove_one_comma) {
    result.pop_back();
  }
  result.append("]");
  return result;
}
}  // end of namespace packet
}  // end of namespace confui
}  // end of namespace cuttlefish
