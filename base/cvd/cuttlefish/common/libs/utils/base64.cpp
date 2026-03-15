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

#include "cuttlefish/common/libs/utils/base64.h"

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include <openssl/base64.h>

#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<std::string> EncodeBase64(const void* data, size_t size) {
  size_t max_length = 0;
  CF_EXPECT_NE(EVP_EncodedLength(&max_length, size), 0);

  std::string out;
  out.resize(max_length);
  ssize_t enc_res =
      EVP_EncodeBlock(reinterpret_cast<uint8_t*>(out.data()),
                      reinterpret_cast<const uint8_t*>(data), size);
  CF_EXPECT_GE(enc_res, 0);
  out.resize(enc_res);  // Don't count the terminating \0 character
  return out;
}

Result<std::string> EncodeBase64(std::string_view data) {
  return CF_EXPECT(EncodeBase64(data.data(), data.size()));
}

Result<std::vector<uint8_t>> DecodeBase64(const std::string& data) {
  std::vector<uint8_t> buffer(data.size());
  size_t actual_len = 0;
  CF_EXPECT_EQ(EVP_DecodeBase64(buffer.data(), &actual_len, buffer.size(),
                                reinterpret_cast<const uint8_t*>(data.data()),
                                data.size()),
               1);
  buffer.resize(actual_len);

  return buffer;
}

}  // namespace cuttlefish
