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

#include "host/libs/input_connector/input_devices.h"

#include <linux/input.h>

#include "host/libs/input_connector/event_buffer.h"

namespace cuttlefish {

Result<void> TouchDevice::SendTouchEvent(int x, int y, bool down) {
  auto buffer = CreateBuffer(event_type(), 4);
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");
  buffer->AddEvent(EV_ABS, ABS_X, x);
  buffer->AddEvent(EV_ABS, ABS_Y, y);
  buffer->AddEvent(EV_KEY, BTN_TOUCH, down);
  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  CF_EXPECT(WriteEvents(*buffer));
  return {};
}

Result<void> TouchDevice::SendMultiTouchEvent(
    const std::vector<MultitouchSlot>& slots, bool down) {
  auto buffer = CreateBuffer(event_type(), 1 + 7 * slots.size());
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");

  for (auto& f : slots) {
    auto this_id = f.id;
    auto this_x = f.x;
    auto this_y = f.y;

    auto is_new_contact = !HasSlot(this, this_id);

    // Make sure to call HasSlot before this line or it will always return true
    auto this_slot = GetOrAcquireSlot(this, this_id);

    // BTN_TOUCH DOWN must be the first event in a series
    if (down && is_new_contact) {
      buffer->AddEvent(EV_KEY, BTN_TOUCH, 1);
    }

    buffer->AddEvent(EV_ABS, ABS_MT_SLOT, this_slot);
    if (down) {
      if (is_new_contact) {
        // We already assigned this slot to this source and id combination, we
        // could use any tracking id for the slot as long as it's greater than 0
        buffer->AddEvent(EV_ABS, ABS_MT_TRACKING_ID, NewTrackingId());
      }
      buffer->AddEvent(EV_ABS, ABS_MT_POSITION_X, this_x);
      buffer->AddEvent(EV_ABS, ABS_MT_POSITION_Y, this_y);
    } else {
      // released touch
      buffer->AddEvent(EV_ABS, ABS_MT_TRACKING_ID, -1);
      ReleaseSlot(this, this_id);
      buffer->AddEvent(EV_KEY, BTN_TOUCH, 0);
    }
  }

  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  CF_EXPECT(WriteEvents(*buffer));
  return {};
}

bool TouchDevice::HasSlot(void* source, int32_t id) {
  std::lock_guard<std::mutex> lock(slots_mtx_);
  return slots_by_source_and_id_.find({source, id}) !=
         slots_by_source_and_id_.end();
}

int32_t TouchDevice::GetOrAcquireSlot(void* source, int32_t id) {
  std::lock_guard<std::mutex> lock(slots_mtx_);
  auto slot_it = slots_by_source_and_id_.find({source, id});
  if (slot_it != slots_by_source_and_id_.end()) {
    return slot_it->second;
  }
  return slots_by_source_and_id_[std::make_pair(source, id)] = UseNewSlot();
}

void TouchDevice::ReleaseSlot(void* source, int32_t id) {
  std::lock_guard<std::mutex> lock(slots_mtx_);
  auto slot_it = slots_by_source_and_id_.find({source, id});
  if (slot_it == slots_by_source_and_id_.end()) {
    return;
  }
  active_slots_[slot_it->second] = false;
  slots_by_source_and_id_.erase(slot_it);
}

void TouchDevice::OnDisconnectedSource(void* source) {
  std::lock_guard<std::mutex> lock(slots_mtx_);
  auto it = slots_by_source_and_id_.begin();
  while (it != slots_by_source_and_id_.end()) {
    if (it->first.first == source) {
      active_slots_[it->second] = false;
      it = slots_by_source_and_id_.erase(it);
    } else {
      ++it;
    }
  }
}

int32_t TouchDevice::UseNewSlot() {
  // This is not the most efficient implementation for a large number of
  // slots, but that case should be extremely rare. For the typical number of
  // slots iterating over a vector is likely faster than using other data
  // structures.
  for (auto slot = 0; slot < active_slots_.size(); ++slot) {
    if (!active_slots_[slot]) {
      active_slots_[slot] = true;
      return slot;
    }
  }
  active_slots_.push_back(true);
  return active_slots_.size() - 1;
}

Result<void> MouseDevice::SendMoveEvent(int x, int y) {
  auto buffer = CreateBuffer(event_type(), 2);
  CF_EXPECT(buffer != nullptr,
            "Failed to allocate input events buffer for mouse move event !");
  buffer->AddEvent(EV_REL, REL_X, x);
  buffer->AddEvent(EV_REL, REL_Y, y);
  CF_EXPECT(conn().WriteEvents(buffer->data(), buffer->size()));
  return {};
}

Result<void> MouseDevice::SendButtonEvent(int button, bool down) {
  auto buffer = CreateBuffer(event_type(), 2);
  CF_EXPECT(buffer != nullptr,
            "Failed to allocate input events buffer for mouse button event !");
  std::vector<int> buttons = {BTN_LEFT, BTN_MIDDLE, BTN_RIGHT, BTN_BACK,
                              BTN_FORWARD};
  CF_EXPECT(button < (int)buttons.size(),
            "Unknown mouse event button: " << button);
  buffer->AddEvent(EV_KEY, buttons[button], down);
  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  CF_EXPECT(conn().WriteEvents(buffer->data(), buffer->size()));
  return {};
}

Result<void> MouseDevice::SendWheelEvent(int pixels) {
  auto buffer = CreateBuffer(event_type(), 2);
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");
  buffer->AddEvent(EV_REL, REL_WHEEL, pixels);
  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  CF_EXPECT(conn().WriteEvents(buffer->data(), buffer->size()));
  return {};
}

Result<void> KeyboardDevice::SendEvent(uint16_t code, bool down) {
  auto buffer = CreateBuffer(event_type(), 2);
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");
  buffer->AddEvent(EV_KEY, code, down);
  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  CF_EXPECT(conn().WriteEvents(buffer->data(), buffer->size()));
  return {};
}

Result<void> RotaryDevice::SendEvent(int pixels) {
  auto buffer = CreateBuffer(event_type(), 2);
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");
  buffer->AddEvent(EV_REL, REL_WHEEL, pixels);
  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  CF_EXPECT(conn().WriteEvents(buffer->data(), buffer->size()));
  return {};
}

Result<void> SwitchesDevice::SendEvent(uint16_t code, bool state) {
  auto buffer = CreateBuffer(event_type(), 2);
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");
  buffer->AddEvent(EV_SW, code, state);
  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  CF_EXPECT(conn().WriteEvents(buffer->data(), buffer->size()));
  return {};
}

}  // namespace cuttlefish
