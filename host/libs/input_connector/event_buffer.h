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

#include <cstdint>
#include <cstdlib>

#include <memory>

namespace cuttlefish {
enum class InputEventType {
  Virtio,
  Evdev,
};

class EventBuffer {
 public:
  virtual ~EventBuffer() = default;
  virtual void AddEvent(uint16_t type, uint16_t code, int32_t value) = 0;
  virtual size_t size() const = 0;
  virtual const void* data() const = 0;
};

std::unique_ptr<EventBuffer> CreateBuffer(InputEventType event_type,
                                          size_t num_events);

}  // namespace cuttlefish
