/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common/libs/utils/base64.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <openssl/base64.h>

namespace cuttlefish {

namespace {

// EVP_EncodedLength is boringssl specific so it can't be used outside of
// android.
std::optional<size_t> EncodedLength(size_t len) {
  if (len + 2 < len) {
    return std::nullopt;
  }
  len += 2;
  len /= 3;

  if (((len << 2) >> 2) != len) {
    return std::nullopt;
  }
  len <<= 2;

  if (len + 1 < len) {
    return std::nullopt;
  }
  len++;

  return {len};
}

// EVP_DecodedLength is boringssl specific so it can't be used outside of
// android.
std::optional<size_t> DecodedLength(size_t len) {
  if (len % 4 != 0) {
    return std::nullopt;
  }

  return {(len / 4) * 3};
}

}  // namespace

bool EncodeBase64(const void *data, std::size_t size, std::string *out) {
  auto len_res = EncodedLength(size);
  if (!len_res) {
    return false;
  }
  out->resize(*len_res);
  auto enc_res =
      EVP_EncodeBlock(reinterpret_cast<std::uint8_t *>(out->data()),
                      reinterpret_cast<const std::uint8_t *>(data), size);
  if (enc_res < 0) {
    return false;
  }
  out->resize(enc_res);  // Don't count the terminating \0 character
  return true;
}

bool DecodeBase64(const std::string &data, std::vector<std::uint8_t> *buffer) {
  auto len_res = DecodedLength(data.size());
  if (!len_res) {
    return false;
  }
  auto out_len = *len_res;
  buffer->resize(out_len);
  auto actual_len = EVP_DecodeBlock(buffer->data(),
                                reinterpret_cast<const uint8_t *>(data.data()),
                                data.size());
  if (actual_len < 0) {
    return false;
  }

  // DecodeBlock leaves null characters at the end of the buffer when the
  // decoded message is not a multiple of 3.
  while (!buffer->empty() && buffer->back() == '\0') {
    buffer->pop_back();
  }

  return true;
}

}  // namespace cuttlefish
