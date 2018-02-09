#pragma once
/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <memory>

#include "common/vsoc/lib/typed_region_view.h"
#include "common/vsoc/shm/input_events_layout.h"
#include "uapi/vsoc_shm.h"

namespace vsoc {
namespace input_events {

struct InputEvent {
  uint16_t type;
  uint16_t code;
  uint32_t value;
};

class InputEventsRegionView
    : public vsoc::TypedRegionView<
          InputEventsRegionView,
          vsoc::layout::input_events::InputEventsLayout> {
 public:
  static const int kMaxEventsPerPacket;
  // Generates a touch event, may returns true if successful, false if there was
  // an error, most likely that the queue is full.
  bool HandleSingleTouchEvent(bool down, int x, int y);
  bool HandlePowerButtonEvent(bool down);
  bool HandleKeyboardEvent(bool down, uint16_t key_code);

  // TODO(jemoreira): HandleMultiTouchEvent()...

  // Read input events from the queue, waits if there are none available.
  // Returns the number of events read or a negative value in case of an error
  // (most likely the next packet in the queue is larger than the buffer
  // provided).
  intptr_t GetScreenEventsOrWait(InputEvent* buffer, int max_event_count);
  intptr_t GetKeyboardEventsOrWait(InputEvent* buffer, int max_event_count);
  intptr_t GetPowerButtonEventsOrWait(InputEvent* buffer, int max_event_count);

};
}  // namespace input_events
}  // namespace vsoc
