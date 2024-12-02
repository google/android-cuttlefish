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

#include "host/libs/input_connector/event_buffer.h"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

#include <linux/input.h>

namespace cuttlefish {
namespace {

struct virtio_input_event {
  uint16_t type;
  uint16_t code;
  int32_t value;
};

template <typename T>
struct EventBufferImpl : public EventBuffer {
  EventBufferImpl(size_t num_events) { buffer_.reserve(num_events); }
  void AddEvent(uint16_t type, uint16_t code, int32_t value) override {
    buffer_.push_back({.type = type, .code = code, .value = value});
  }
  const void* data() const override { return buffer_.data(); }
  std::size_t size() const override { return buffer_.size() * sizeof(T); }

 private:
  std::vector<T> buffer_;
};

}  // namespace

std::unique_ptr<EventBuffer> CreateBuffer(InputEventType event_type,
                                          size_t num_events) {
  switch (event_type) {
    case InputEventType::Virtio:
      return std::unique_ptr<EventBuffer>(
          new EventBufferImpl<virtio_input_event>(num_events));
    case InputEventType::Evdev:
      return std::unique_ptr<EventBuffer>(
          new EventBufferImpl<input_event>(num_events));
  }
}

}  // namespace cuttlefish
