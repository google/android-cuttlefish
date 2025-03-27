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

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/libs/input_connector/event_buffer.h"
#include "host/libs/input_connector/input_connection.h"
#include "host/libs/input_connector/input_connector.h"

namespace cuttlefish {

class InputDevice {
 public:
  InputDevice(InputConnection conn) : conn_(conn) {}
  virtual ~InputDevice() = default;

 protected:
  Result<void> WriteEvents(const EventBuffer& buffer);

 private:
  InputConnection conn_;
};

class TouchDevice : public InputDevice {
 public:
  TouchDevice(InputConnection conn)
      : InputDevice(conn) {}

  Result<void> SendTouchEvent(int x, int y, bool down);

  Result<void> SendMultiTouchEvent(const std::vector<MultitouchSlot>& slots,
                                   bool down);

  // The InputConnector holds state of on-going touch contacts. Event sources
  // that can't produce multi touch events should call this function when it's
  // known they won't produce any more events (because, for example, the
  // streaming client disconnected) to make sure no stale touch contacts
  // remain. This addresses issues arising from clients disconnecting in the
  // middle of a touch action.
  void OnDisconnectedSource(void* source);

 private:
  bool HasSlot(void* source, int32_t id);

  int32_t GetOrAcquireSlot(void* source, int32_t id);

  void ReleaseSlot(void* source, int32_t id);

  size_t NumActiveSlots() {
    std::lock_guard<std::mutex> lock(slots_mtx_);
    return slots_by_source_and_id_.size();
  }

  int NewTrackingId() { return ++tracking_id_; }

  int32_t UseNewSlot();

  std::mutex slots_mtx_;
  std::map<std::pair<void*, int32_t>, int32_t> slots_by_source_and_id_;
  std::vector<bool> active_slots_;
  std::atomic<int> tracking_id_ = 0;
};

class MouseDevice : public InputDevice {
 public:
  MouseDevice(InputConnection conn)
      : InputDevice(conn) {}

  Result<void> SendMoveEvent(int x, int y);
  Result<void> SendButtonEvent(int button, bool down);
  Result<void> SendWheelEvent(int pixels);
};

class KeyboardDevice : public InputDevice {
 public:
  KeyboardDevice(InputConnection conn)
      : InputDevice(conn) {}

  Result<void> SendEvent(uint16_t code, bool down);
};

class RotaryDevice : public InputDevice {
 public:
  RotaryDevice(InputConnection conn)
      : InputDevice(conn) {}

  Result<void> SendEvent(int pixels);
};

class SwitchesDevice : public InputDevice {
 public:
  SwitchesDevice(InputConnection conn)
      : InputDevice(conn) {}

  Result<void> SendEvent(uint16_t code, bool state);
};

}  // namespace cuttlefish
