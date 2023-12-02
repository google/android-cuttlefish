/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <vector>

#include "common/libs/utils/result.h"

namespace cuttlefish {

struct MultitouchSlot {
  int32_t id;
  int32_t x;
  int32_t y;
};

class InputConnector {
 public:
  virtual ~InputConnector() = default;
  virtual Result<void> SendTouchEvent(const std::string& display, int x, int y,
                                      bool down) = 0;
  // The source parameter is used to differentiate between events coming from
  // different sources with the same id. For example when multiple clients are
  // connected and sending touch events at the same time.
  virtual Result<void> SendMultiTouchEvent(
      void* source, const std::string& device_label,
      const std::vector<MultitouchSlot>& slots, bool down) = 0;
  // The InputConnector holds state of on-going touch contacts. Event sources
  // that can produce multi touch events should call this function when it's
  // known they won't produce any more events (because, for example, the
  // streaming client disconnected) to make sure no stale touch contacts remain.
  // This addresses issues arising from clients disconnecting in the middle of a
  // touch action.
  virtual void OnDisconnectedSource(void* source) = 0;
  virtual Result<void> SendKeyboardEvent(uint16_t code, bool down) = 0;
  virtual Result<void> SendRotaryEvent(int pixels) = 0;
  virtual Result<void> SendSwitchesEvent(uint16_t code, bool state) = 0;
};

}  // namespace cuttlefish
