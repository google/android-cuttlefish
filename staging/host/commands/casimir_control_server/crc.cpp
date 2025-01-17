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

#include "host/commands/casimir_control_server/crc.h"

#include "common/libs/utils/result.h"

namespace cuttlefish {

namespace {
static std::vector<uint8_t> Crc16(const std::vector<uint8_t>& data,
                                  uint16_t initial, bool invert) {
  uint16_t w_crc = initial;

  for (uint8_t byte : data) {
    byte ^= (w_crc & 0x00FF);
    byte ^= (byte << 4) & 0xFF;
    w_crc = (w_crc >> 8) ^ ((byte << 8) & 0xFFFF) ^ ((byte << 3) & 0xFFFF) ^
            ((byte >> 4) & 0xFFFF);
  }

  if (invert) {
    w_crc = ~w_crc;
  }

  return {static_cast<uint8_t>(w_crc & 0xFF),
          static_cast<uint8_t>((w_crc >> 8) & 0xFF)};
}

static std::vector<uint8_t> Crc16A(const std::vector<uint8_t>& data) {
  return Crc16(data, 0x6363, false);
}

static std::vector<uint8_t> Crc16B(const std::vector<uint8_t>& data) {
  return Crc16(data, 0xFFFF, true);
}
}  // namespace

Result<std::vector<uint8_t>> WithCrc16A(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> newData = data;
  std::vector<uint8_t> crc = Crc16A(newData);
  newData.insert(newData.end(), crc.begin(), crc.end());
  return newData;
}

Result<std::vector<uint8_t>> WithCrc16B(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> newData = data;
  std::vector<uint8_t> crc = Crc16B(newData);
  newData.insert(newData.end(), crc.begin(), crc.end());
  return newData;
}

}  // namespace cuttlefish
