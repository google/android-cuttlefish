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

namespace cuttlefish {
namespace confui {
namespace packet {
static std::optional<std::vector<std::uint8_t>> ReadRawData(SharedFD s) {
  if (!s->IsOpen()) {
    ConfUiLog(ERROR) << "file, socket, etc, is not open to read";
    return std::nullopt;
  }
  packet::PayloadHeader p;
  auto nread = ReadExactBinary(s, &p);

  if (nread != sizeof(p)) {
    ConfUiLog(ERROR) << nread << " and sizeof(p) = " << sizeof(p)
                     << " not matching";
    return std::nullopt;
  }
  if (p.payload_length_ == 0) {
    return {{}};
  }

  if (p.payload_length_ >= packet::kMaxPayloadLength) {
    ConfUiLog(ERROR) << "Payload length " << p.payload_length_
                     << " must be less than " << packet::kMaxPayloadLength;
    return std::nullopt;
  }

  std::unique_ptr<char[]> buf{new char[p.payload_length_ + 1]};
  nread = ReadExact(s, buf.get(), p.payload_length_);
  buf[p.payload_length_] = 0;
  if (nread != p.payload_length_) {
    ConfUiLog(ERROR) << "The length ReadRawData read does not match.";
    return std::nullopt;
  }
  std::vector<std::uint8_t> result{buf.get(), buf.get() + nread};

  return {result};
}

static std::optional<ParsedPacket> ParseRawData(
    const std::vector<std::uint8_t>& data_to_parse) {
  /*
   * data_to_parse has 0 in it, so it is not exactly "your (text) std::string."
   * If we type-cast data_to_parse to std::string and use 3rd party std::string-
   * processing libraries, the outcome might be incorrect. However, the header
   * part has no '\0' in it, and is actually a sequence of letters, or a text.
   * So, we use android::base::Split() to take the header
   *
   */
  std::string as_string{data_to_parse.begin(), data_to_parse.end()};
  auto tokens = android::base::Split(as_string, ":");
  CHECK(tokens.size() >= 3)
      << "Raw packet for confirmation UI must have at least"
      << " three components.";
  /**
   * Here is how the raw data, i.e. tokens[2:] looks like
   *
   * n:l[0]:l[1]:l[2]:...:l[n-1]:data[0]data[1]data[2]...data[n]
   *
   * Thus it basically has the number of items, the lengths of each item,
   * and the byte representation of each item. n and l[i] are separated by ':'
   * Note that the byte representation may have ':' in it. This could mess
   * up the parsing if we totally depending on ':' separation.
   *
   * However, it is safe to assume that there's no ':' inside n or
   * the string for l[i]. So, we do anyway split the data_to_parse by ':',
   * and take n and from l[0] through l[n-1] only.
   */
  std::string session_id = tokens[0];
  std::string cmd_type = tokens[1];
  if (!IsOnlyDigits(tokens[2])) {
    ConfUiLog(ERROR) << "Token[2] of the ConfUi packet should be a number";
    return std::nullopt;
  }
  const int n = std::stoi(tokens[2]);

  if (n + 2 > tokens.size()) {
    ConfUiLog(ERROR) << "The ConfUi packet is ill-formatted.";
    return std::nullopt;
  }
  ConfUiPacketInfo data_to_return;
  std::vector<int> lengths;
  lengths.reserve(n);
  for (int i = 1; i <= n; i++) {
    if (!IsOnlyDigits(tokens[2 + i])) {
      ConfUiLog(ERROR) << tokens[2 + i] << " should be a number but is not.";
      return std::nullopt;
    }
    lengths.emplace_back(std::stoi(tokens[2 + i]));
  }
  // to find the first position of the non-header part
  int pos = 0;
  // 3 for three ":"s
  pos += tokens[0].size() + tokens[1].size() + tokens[2].size() + 3;
  for (int i = 1; i <= n; i++) {
    pos += tokens[2 + i].size() + 1;
  }
  int expected_total_length = pos;
  for (auto const len : lengths) {
    expected_total_length += len;
  }
  if (expected_total_length != data_to_parse.size()) {
    ConfUiLog(ERROR) << "expected length in ParseRawData is "
                     << expected_total_length << " while the actual length is "
                     << data_to_parse.size();
    return std::nullopt;
  }
  for (const auto len : lengths) {
    if (len == 0) {
      // push null vector or whatever empty, appropriately-typed
      // container
      data_to_return.emplace_back(std::vector<std::uint8_t>{});
      continue;
    }
    data_to_return.emplace_back(data_to_parse.begin() + pos,
                                data_to_parse.begin() + pos + len);
    pos = pos + len;
  }
  ParsedPacket result{session_id, cmd_type, data_to_return};
  return {result};
}

std::optional<ParsedPacket> ReadPayload(SharedFD s) {
  auto raw_data = ReadRawData(s);
  if (!raw_data) {
    ConfUiLog(ERROR) << "raw data returned std::nullopt";
    return std::nullopt;
  }
  auto parsed_result = ParseRawData(raw_data.value());
  if (!parsed_result) {
    ConfUiLog(ERROR) << "parsed result returns nullopt";
    return std::nullopt;
  }
  return parsed_result;
}
}  // end of namespace packet
}  // end of namespace confui
}  // end of namespace cuttlefish
