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
#include <memory>
#include <string>
#include <vector>

#include <android/hardware/keymaster/4.0/types.h>

#include <cn-cbor/cn-cbor.h>

namespace cuttlefish {
namespace confui {

/** take prompt_text_, extra_data
 * returns CBOR map, created with the two
 *
 * Usage:
 *  if (IsOk()) GetMessage()
 *
 * The CBOR map is used to create signed confirmation
 */
class Cbor {
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
  Cbor(const std::string& prompt_text,
       const std::vector<std::uint8_t>& extra_data)
      : prompt_text_(prompt_text),
        extra_data_(extra_data),
        buffer_status_{Error::OK},
        buffer_(kMax + 1) {
    Init();
  }

  bool IsOk() const { return buffer_status_ == Error::OK; }
  Error GetErrorCode() const { return buffer_status_; }
  bool IsMessageTooLong() const { return buffer_status_ == Error::OUT_OF_DATA; }
  bool IsMalformedUtf8() const {
    return buffer_status_ == Error::MALFORMED_UTF8;
  }
  // call this only when IsOk() returns true
  std::vector<std::uint8_t>&& GetMessage();

  /** When encoded, the Cbor object should not exceed this limit in terms of
   * size in bytes
   */
  const std::uint32_t kMax = static_cast<std::uint32_t>(MessageSize::MAX);

 private:
  class CborDeleter {
   public:
    void operator()(cn_cbor* ptr) { cn_cbor_free(ptr); }
  };

  std::unique_ptr<cn_cbor, CborDeleter> cb_map_;
  std::string prompt_text_;
  std::vector<std::uint8_t> extra_data_;
  Error buffer_status_;
  std::vector<std::uint8_t> buffer_;

  void Init();
  Error CheckUTF8Copy(const std::string& text);
};

}  // namespace confui
}  // end of namespace cuttlefish
