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

#include "host/libs/confui/cbor.h"

#include "common/libs/confui/confui.h"

namespace cuttlefish {
namespace confui {
/**
 * basically, this creates a map as follows:
 * {"prompt" : prompt_text_in_UTF8,
 *  "extra"  : extra_data_in_bytes}
 */
void Cbor::Init() {
  cn_cbor_errback err;
  cb_map_ = std::unique_ptr<cn_cbor, CborDeleter>(cn_cbor_map_create(&err));

  buffer_status_ = CheckUTF8Copy(prompt_text_);
  if (!IsOk()) {
    return;
  }

  auto cb_prompt_as_value = cn_cbor_string_create(prompt_text_.data(), &err);
  auto cb_extra_data_as_value =
      cn_cbor_data_create(extra_data_.data(), extra_data_.size(), &err);
  cn_cbor_mapput_string(cb_map_.get(), "prompt", cb_prompt_as_value, &err);
  cn_cbor_mapput_string(cb_map_.get(), "extra", cb_extra_data_as_value, &err);

  // cn_cbor_encoder_write wants buffer_ to have a trailing 0 at the end
  auto n_chars =
      cn_cbor_encoder_write(buffer_.data(), 0, buffer_.size(), cb_map_.get());
  ConfUiLog(ERROR) << "Cn-cbor encoder wrote " << n_chars << " while "
                   << "kMax is " << kMax;
  if (n_chars < 0) {
    // it's either message being too long, or a potential cn_cbor bug
    ConfUiLog(ERROR) << "Cn-cbor returns -1 which is likely message too long.";
    buffer_status_ = Error::OUT_OF_DATA;
  }
  if (!IsOk()) {
    return;
  }
  buffer_.resize(n_chars);
}

std::vector<std::uint8_t>&& Cbor::GetMessage() { return std::move(buffer_); }

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
