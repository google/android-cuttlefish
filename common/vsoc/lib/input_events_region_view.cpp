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
#include "common/vsoc/lib/input_events_region_view.h"

#include <linux/input.h>
#include <linux/uinput.h>

#include "common/vsoc/lib/circqueue_impl.h"

using vsoc::layout::input_events::InputEventsLayout;

namespace vsoc {
namespace input_events {

namespace {
void InitInputEvent(InputEvent* evt, uint16_t type, uint16_t code, uint32_t value) {
  evt->type = type;
  evt->code = code;
  evt->value = value;
}
}  // namespace

const int InputEventsRegionView::kMaxEventsPerPacket = 4;

bool InputEventsRegionView::HandleSingleTouchEvent(bool down, int x, int y) {
  // TODO(jemoreira): Use multitouch when available
  InputEvent events[4];
  // Make sure to modify kMaxEventPerPacket if more events are sent.
  InitInputEvent(&events[0], EV_ABS, ABS_X, x);
  InitInputEvent(&events[1], EV_ABS, ABS_Y, y);
  InitInputEvent(&events[2], EV_KEY, BTN_TOUCH, down);
  InitInputEvent(&events[3], EV_SYN, 0, 0);
  return 0 <
         data()->touch_screen_queue.Write(
             this, reinterpret_cast<char*>(&events[0]), sizeof(events), true);
}

bool InputEventsRegionView::HandlePowerButtonEvent(bool down) {
  InputEvent events[2];
  InitInputEvent(&events[0], EV_KEY, KEY_POWER, down);
  InitInputEvent(&events[1], EV_SYN, 0, 0);
  return 0 < data()->power_button_queue.Write(
                 this, reinterpret_cast<char*>(&events[0]),
                 sizeof(events), true);
}

bool InputEventsRegionView::HandleKeyboardEvent(bool down, uint16_t key_code) {
  InputEvent events[2];
  InitInputEvent(&events[0], EV_KEY, key_code, down);
  InitInputEvent(&events[1], EV_SYN, 0, 0);
  return 0 <
         data()->keyboard_queue.Write(this, reinterpret_cast<char*>(&events[0]),
                                      sizeof(events), true);
}

intptr_t InputEventsRegionView::GetScreenEventsOrWait(InputEvent* evt,
                                                      int max_event_count) {
  intptr_t ret = this->data()->touch_screen_queue.Read(
      this, reinterpret_cast<char*>(evt), sizeof(InputEvent) * max_event_count);
  if (ret < 0) {
    return ret;
  }
  return ret / sizeof(InputEvent);
}

intptr_t InputEventsRegionView::GetKeyboardEventsOrWait(InputEvent* evt,
                                                        int max_event_count) {
  intptr_t ret = this->data()->keyboard_queue.Read(
      this, reinterpret_cast<char*>(evt), sizeof(InputEvent) * max_event_count);
  if (ret < 0) {
    return ret;
  }
  return ret / sizeof(InputEvent);
}

intptr_t InputEventsRegionView::GetPowerButtonEventsOrWait(
    InputEvent* evt, int max_event_count) {
  intptr_t ret = this->data()->power_button_queue.Read(
      this, reinterpret_cast<char*>(evt), sizeof(InputEvent) * max_event_count);
  if (ret < 0) {
    return ret;
  }
  return ret / sizeof(InputEvent);
}

#if defined(CUTTLEFISH_HOST)
std::shared_ptr<InputEventsRegionView> InputEventsRegionView::GetInstance(
    const char* domain) {
  return RegionView::GetInstanceImpl<InputEventsRegionView>(
      [](std::shared_ptr<InputEventsRegionView> region, const char* domain) {
        return region->Open(domain);
      },
      domain);
}
#else
std::shared_ptr<InputEventsRegionView> InputEventsRegionView::GetInstance() {
  return RegionView::GetInstanceImpl<InputEventsRegionView>(
      std::mem_fn(&InputEventsRegionView::Open));
}
#endif
}
}
