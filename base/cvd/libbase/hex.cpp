/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "android-base/hex.h"

#include "android-base/logging.h"

namespace android {
namespace base {

std::string HexString(const void* bytes, size_t len) {
  CHECK(bytes != nullptr || len == 0) << bytes << " " << len;

  // b/132916539: Doing this the 'C way', std::setfill triggers ubsan implicit conversion
  const uint8_t* bytes8 = static_cast<const uint8_t*>(bytes);
  const char chars[] = "0123456789abcdef";
  std::string result;
  result.resize(len * 2);

  for (size_t i = 0; i < len; i++) {
    result[2 * i] = chars[bytes8[i] >> 4];
    result[2 * i + 1] = chars[bytes8[i] & 0xf];
  }

  return result;
}

static uint8_t HexNybbleToValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'Z') {
    return c - 'A' + 10;
  }
  return 0xff;
}

bool HexToBytes(const std::string& hex, std::vector<uint8_t>* bytes) {
  if (hex.size() % 2 != 0) {
    LOG(ERROR) << "HexToBytes: Invalid size: " << hex.size();
    return false;
  }
  bytes->resize(hex.size() / 2);
  for (unsigned i = 0; i < bytes->size(); i++) {
    uint8_t hi = HexNybbleToValue(hex[i * 2]);
    uint8_t lo = HexNybbleToValue(hex[i * 2 + 1]);
    if (lo > 0xf || hi > 0xf) {
      LOG(ERROR) << "HexToBytes: Invalid characters: " << hex[i * 2] << ", " << hex[i * 2 + 1];
      return false;
    }
    (*bytes)[i] = (hi << 4) | lo;
  }
  return true;
}

}  // namespace base
}  // namespace android
