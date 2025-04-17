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
#include <string>
#include <vector>

#include <openssl/base64.h>

namespace cuttlefish {

bool EncodeBase64(const void *data, std::size_t size, std::string *out) {
  std::size_t max_length = 0;
  if (EVP_EncodedLength(&max_length, size) == 0) {
    return false;
  }

  out->resize(max_length);
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
  buffer->resize(data.size());
  std::size_t actual_len = 0;
  int success = EVP_DecodeBase64(buffer->data(), &actual_len, buffer->size(),
                                 reinterpret_cast<const uint8_t *>(data.data()),
                                 data.size());
  if (success != 1) {
    return false;
  }
  buffer->resize(actual_len);

  return true;
}

}  // namespace cuttlefish
