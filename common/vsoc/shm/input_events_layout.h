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

#include "common/vsoc/shm/base.h"
#include "common/vsoc/shm/circqueue.h"

// Memory layout for region carrying input events from host to guest

namespace vsoc {
namespace layout {

namespace input_events {

struct InputEventsLayout : public RegionLayout {
  static const char* region_name;
  // Event queues for the different input devices supported. Both the power
  // button and the keyboard need only generate 2 input events for every
  // 'hardware' event, so 16 bytes are enough, however when the touchscreen has
  // multitouch enabled the number of generated events is significantly higher.
  CircularPacketQueue<10, 256> touch_screen_queue;
  CircularPacketQueue<10, 16> keyboard_queue;
  CircularPacketQueue<10, 16> power_button_queue;
};

}  // namespace input_events
}  // namespace layout
}  // namespace vsoc
