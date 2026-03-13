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

#include "cuttlefish/host/libs/confui/confui_cbor.h"

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "cbor.h"

#include "cuttlefish/common/libs/confui/confui.h"

namespace cuttlefish {
namespace confui {
/**
 * basically, this creates a map as follows:
 * {"prompt" : prompt_text_in_UTF8,
 *  "extra"  : extra_data_in_bytes}
 */
void Cbor::Init() {
  cb_map_.reset(cbor_new_definite_map(2));

  buffer_status_ = CheckUTF8Copy(prompt_text_);
  if (!IsOk()) {
    return;
  }

  cbor_item_t* cb_prompt = cbor_build_string(prompt_text_.c_str());
  if (cb_prompt == nullptr) {
    buffer_status_ = Error::OUT_OF_DATA;
    return;
  }
  auto success = cbor_map_add(
      cb_map_.get(), (struct cbor_pair){.key = cbor_move(cbor_build_string("prompt")),
                                  .value = cbor_move(cb_prompt)});
  if (!success) {
    // Shouldn't happen, the map has capacity for 2.
    buffer_status_ = Error::OUT_OF_DATA;
    return;
  }
  cbor_item_t* cb_extra_data =
      cbor_build_bytestring(extra_data_.data(), extra_data_.size());
  if (cb_extra_data == nullptr) {
    buffer_status_ = Error::OUT_OF_DATA;
    return;
  }
  success = cbor_map_add(
      cb_map_.get(), (struct cbor_pair){.key = cbor_move(cbor_build_string("extra")),
                                  .value = cbor_move(cb_extra_data)});
  if (!success) {
    // Shouldn't happen, the map has capacity for 2.
    buffer_status_ = Error::MALFORMED;
    return;
  }

  buffer_.resize(cbor_serialized_size(cb_map_.get()));
  // Safe to ignore return value because the buffer was properly resized
  (void)cbor_serialize_map(cb_map_.get(), buffer_.data(), buffer_.size());
}

std::vector<uint8_t>&& Cbor::GetMessage() { return std::move(buffer_); }

Cbor::Error Cbor::CheckUTF8Copy(const std::string& text) {
  auto begin = text.cbegin();
  auto end = text.cend();

  if (!IsOk()) {
    return buffer_status_;
  }

  uint32_t multi_byte_length = 0;
  Cbor::Error err_code = buffer_status_;  // OK

  while (begin != end) {
    if (multi_byte_length) {
      // parsing multi byte character - must start with 10xxxxxx
      --multi_byte_length;
      if ((*begin & 0xc0) != 0x80) {
        return Cbor::Error::MALFORMED_UTF8;
      }
    } else if (!((*begin) & 0x80)) {
      // 7bit character -> nothing to be done
    } else {
      // msb is set and we were not parsing a multi byte character
      // so this must be a header byte
      char c = *begin << 1;
      while (c & 0x80) {
        ++multi_byte_length;
        c <<= 1;
      }
      // headers of the form 10xxxxxx are not allowed
      if (multi_byte_length < 1) {
        return Cbor::Error::MALFORMED_UTF8;
      }
      // chars longer than 4 bytes are not allowed (multi_byte_length does not
      // count the header thus > 3
      if (multi_byte_length > 3) {
        return Cbor::Error::MALFORMED_UTF8;
      }
    }
    ++begin;
  }
  // if the string ends in the middle of a multi byte char it is invalid
  if (multi_byte_length) {
    return Cbor::Error::MALFORMED_UTF8;
  }
  return err_code;
}
}  // end of namespace confui
}  // end of namespace cuttlefish
