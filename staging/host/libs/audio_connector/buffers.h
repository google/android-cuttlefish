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
#pragma once

#include <cinttypes>
#include <functional>

#include "host/libs/audio_connector/shm_layout.h"

namespace cuttlefish {

enum class Status : uint32_t {
  Ok = 0x8000,
  BadMessage,
  NotSupported,
  IOError,
};

using OnConsumedCb = std::function<void(AudioStatus, uint32_t /*latency*/,
                                        uint32_t /*consumed length*/)>;

// Wraps and provides access to audio buffers sent by the client.
// Objects of this class can only be moved, not copied. Destroying a buffer
// without sending the status to the client is a bug so the program aborts in
// those cases.
class ShmBuffer {
 public:
  ShmBuffer(const virtio_snd_pcm_xfer& header, uint32_t len,
            OnConsumedCb on_consumed)
      : header_(header), len_(len), on_consumed_(on_consumed) {}
  ShmBuffer(const ShmBuffer& other) = delete;
  ShmBuffer(ShmBuffer&& other);
  ShmBuffer& operator=(const ShmBuffer& other) = delete;

  ~ShmBuffer();

  uint32_t stream_id() const;
  uint32_t len() const { return len_; }

  void SendStatus(AudioStatus status, uint32_t latency_bytes,
                  uint32_t consumed_len);

 private:
  const virtio_snd_pcm_xfer header_;
  const uint32_t len_;
  OnConsumedCb on_consumed_;
  bool status_sent_ = false;
};

class TxBuffer : public ShmBuffer {
 public:
  TxBuffer(const virtio_snd_pcm_xfer& header, const volatile uint8_t* buffer,
           uint32_t len, OnConsumedCb on_consumed)
      : ShmBuffer(header, len, on_consumed), buffer_(buffer) {}
  TxBuffer(const TxBuffer& other) = delete;
  TxBuffer(TxBuffer&& other) = default;
  TxBuffer& operator=(const TxBuffer& other) = delete;

  const volatile uint8_t* get() const { return buffer_; }

 private:
  const volatile uint8_t* const buffer_;
};

class RxBuffer : public ShmBuffer {
 public:
  RxBuffer(const virtio_snd_pcm_xfer& header, volatile uint8_t* buffer,
           uint32_t len, OnConsumedCb on_consumed)
      : ShmBuffer(header, len, on_consumed), buffer_(buffer) {}
  RxBuffer(const RxBuffer& other) = delete;
  RxBuffer(RxBuffer&& other) = default;
  RxBuffer& operator=(const RxBuffer& other) = delete;

  volatile uint8_t* get() const { return buffer_; }

 private:
  volatile uint8_t* const buffer_;
};

}  // namespace cuttlefish
