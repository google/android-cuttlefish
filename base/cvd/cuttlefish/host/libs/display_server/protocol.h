/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <stdint.h>

namespace cuttlefish::display_server {

inline constexpr uint32_t kFrameSlotsPerDisplay = 4;

enum class ClientCommand : uint8_t {
  kReady = 1,
};

enum class MsgType : uint32_t {
  kDisplayCreated = 1,
  kDisplayDestroyed = 2,
  kFrame = 3,
};

struct DisplayMsg {
  MsgType type;
  uint32_t display_number;
  uint32_t width;
  uint32_t height;
  uint32_t fourcc_format;
  uint32_t stride_bytes;
  uint32_t slot;
  char shm_name[64];
};

}  // namespace cuttlefish::display_server
