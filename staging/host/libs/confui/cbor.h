/*
 * Copyright 2021, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <android/hardware/keymaster/4.0/types.h>

namespace cuttlefish {
namespace confui {

/** take prompt_text_, extra_data
 * returns "message"
 * using "message", communicating with host keymaster,
 * returns signed confirmation when appropriate
 */
class Cbor {
  // TODO(kwstephenkim): replace this using cbor libraries under external
  enum class Error : uint32_t {
    OK = 0,
    OUT_OF_DATA = 1,
    MALFORMED = 2,
    MALFORMED_UTF8 = 3,
  };

  enum class MessageSize : uint32_t { MAX = 6144u };

  enum class Type : uint8_t {
    NUMBER = 0,
    NEGATIVE = 1,
    BYTE_STRING = 2,
    TEXT_STRING = 3,
    ARRAY = 4,
    MAP = 5,
    TAG = 6,
    FLOAT = 7,
  };

 public:
  using HardwareAuthToken =
      android::hardware::keymaster::V4_0::HardwareAuthToken;
  Cbor(const std::string& prompt_text,
       const std::vector<std::uint8_t>& extra_data)
      : prompt_text_(prompt_text),
        extra_data_(extra_data),
        buffer_status_{Error::OK} {
    Init();
  }

  bool IsOk() const { return buffer_status_ == Error::OK; }
  Error GetErrorCode() const { return buffer_status_; }
  bool IsMessageTooLong() const { return buffer_status_ == Error::OUT_OF_DATA; }
  bool IsMalformedUtf8() const {
    return buffer_status_ == Error::MALFORMED_UTF8;
  }
  std::vector<std::uint8_t>&& GetMessage() {
    return std::move(formatted_message_buffer_);
  }
  const std::uint32_t kMax = static_cast<std::uint32_t>(MessageSize::MAX);

 private:
  std::string prompt_text_;
  std::vector<std::uint8_t> extra_data_;

  // should be shorter than or equal to buf[std::uint32_t(MessageSize::MAX)]
  std::vector<std::uint8_t> formatted_message_buffer_;
  Error buffer_status_;
  void Init();
  /**
   * formatted_message_buffer_.emplace_back(created_header)
   */
  bool WriteHeader(const Cbor::Type type, const std::uint64_t value);
  bool WriteTextToBuffer(const std::string& text);
  bool WriteBytesToBuffer(const std::vector<std::uint8_t>& bytes);

  inline uint8_t getByte(const uint64_t& v, const uint8_t index) {
    return v >> (index * 8);
  }
  bool WriteBytes(uint64_t value, uint8_t size);
  Error CheckUTF8Copy(const std::string& text);
};
}  // namespace confui
}  // end of namespace cuttlefish
