//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/libs/audio_connector/buffers.h"

#include <android-base/logging.h>

namespace cuttlefish {

ShmBuffer::ShmBuffer(ShmBuffer&& other)
    : header_(std::move(other.header_)),
      len_(std::move(other.len_)),
      on_consumed_(std::move(other.on_consumed_)),
      status_sent_(other.status_sent_) {
  // It's now this buffer's responsibility to send the status.
  other.status_sent_ = true;
}

ShmBuffer::~ShmBuffer() {
  CHECK(status_sent_) << "Disposing of ShmBuffer before setting status";
}

uint32_t ShmBuffer::stream_id() const { return header_.stream_id.as_uint32_t(); }

void ShmBuffer::SendStatus(AudioStatus status, uint32_t latency_bytes,
                          uint32_t consumed_len) {
  on_consumed_(status, latency_bytes, consumed_len);
  status_sent_ = true;
}

}  // namespace cuttlefish
