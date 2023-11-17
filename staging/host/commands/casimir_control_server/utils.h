/*
 * Copyright 2023 The Android Open Source Project
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

#pragma once

#include "common/libs/utils/result.h"

namespace cuttlefish {

static int ByteNumber(char x) {
  x = tolower(x);
  if ('0' <= x && x <= '9') {
    return x - '0';
  } else if ('a' <= x && x <= 'f') {
    return x - 'a' + 10;
  }
  return -1;
}

Result<std::shared_ptr<std::vector<uint8_t>>> BytesArray(
    const std::string& hex_string) {
  CF_EXPECT(hex_string.size() % 2 == 0,
            "Failed to parse input. Must be even size");

  int len = hex_string.size() / 2;
  auto out = std::make_shared<std::vector<uint8_t>>(len);
  for (int i = 0; i < len; i++) {
    int num_h = ByteNumber(hex_string[i * 2]);
    int num_l = ByteNumber(hex_string[i * 2 + 1]);
    CF_EXPECT(num_h >= 0 && num_l >= 0,
              "Failed to parse input. Must only contain [0-9a-fA-F]");
    (*out.get())[i] = num_h * 16 + num_l;
  }

  return out;
}

}  // namespace cuttlefish