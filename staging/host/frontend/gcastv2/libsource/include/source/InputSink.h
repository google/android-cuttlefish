/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <array>
#include <cinttypes>
#include <functional>
#include <memory>
#include <vector>

#include <https/RunLoop.h>

namespace android {

struct InputEventBuffer {
  virtual ~InputEventBuffer() = default;
  virtual void addEvent(uint16_t type, uint16_t code, int32_t value) = 0;
  virtual size_t size() const = 0;
  virtual const void* data() const = 0;
};

struct InputSink : public std::enable_shared_from_this<InputSink> {
  explicit InputSink(std::shared_ptr<RunLoop> runLoop, int serverFd,
                     bool write_virtio_input);
  ~InputSink();
  void start();

  std::unique_ptr<InputEventBuffer> getEventBuffer() const;
  void SendEvents(std::unique_ptr<InputEventBuffer> evt_buffer);

 private:
  std::shared_ptr<RunLoop> mRunLoop;
  int mServerFd;

  int mClientFd;

  std::mutex mLock;
  std::vector<uint8_t> mOutBuffer;
  bool mSendPending;
  bool mWriteVirtioInput;

  void onServerConnection();
  void onSocketRecv();
  void onSocketSend();

  void sendRawEvents(const void* evt_buffer, size_t length);
};

}  // namespace android
