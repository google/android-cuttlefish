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

ShmBuffer::ShmBuffer(const virtio_snd_pcm_xfer& header,
                     volatile uint8_t* buffer, uint32_t len,
                     OnConsumedCb on_consumed)
    : header_(header),
      len_(len),
      on_consumed_(on_consumed),
      // Cast away the volatile qualifier: No one else will touch this buffer
      // until SendStatus is called, at which point a memory fence will be used
      // to ensure reads and writes are completed before the status is sent.
      buffer_(const_cast<uint8_t*>(buffer)) {}

ShmBuffer::ShmBuffer(ShmBuffer&& other)
    : header_(std::move(other.header_)),
      len_(std::move(other.len_)),
      on_consumed_(std::move(other.on_consumed_)),
      status_sent_(other.status_sent_.load()),
      buffer_(other.buffer_) {
  // It's now this buffer's responsibility to send the status.
  other.status_sent_ = true;
}

ShmBuffer::~ShmBuffer() {
  CHECK(status_sent_) << "Disposing of ShmBuffer before setting status";
}

uint32_t ShmBuffer::stream_id() const {
  return header_.stream_id.as_uint32_t();
}

void ShmBuffer::SendStatus(AudioStatus status, uint32_t latency_bytes,
                           uint32_t consumed_len) {
  // Memory order is seq_cst to provide memory fence. It ensures all accesses
  // are completed before the status is sent and the buffer is released.
  CHECK(!status_sent_.exchange(true)) << "Status should only be sent once";
  on_consumed_(status, latency_bytes, consumed_len);
}

}  // namespace cuttlefish
