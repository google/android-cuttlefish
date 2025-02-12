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

#include <vector>

#include "common/libs/utils/cf_endian.h"

namespace cuttlefish {

class EventBuffer {
 public:
  EventBuffer(size_t num_events);

  void AddEvent(uint16_t type, uint16_t code, int32_t value);

  size_t size() const { return buffer_.size() * sizeof(virtio_input_event); }

  const void* data() const { return buffer_.data(); }

 private:
  struct virtio_input_event {
    Le16 type;
    Le16 code;
    Le32 value;
  };

  std::vector<virtio_input_event> buffer_;
};

}  // namespace cuttlefish
