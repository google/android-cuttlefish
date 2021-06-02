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

#include <deque>
#include <vector>

struct lws;

namespace cuttlefish {

class WebSocketHandler {
 public:
  WebSocketHandler(struct lws* wsi);
  virtual ~WebSocketHandler() = default;

  virtual void OnReceive(const uint8_t* msg, size_t len, bool binary) = 0;
  virtual void OnReceive(const uint8_t* msg, size_t len, bool binary,
                         [[maybe_unused]] bool is_final) {
    OnReceive(msg, len, binary);
  }
  virtual void OnConnected() = 0;
  virtual void OnClosed() = 0;

  void EnqueueMessage(const uint8_t* data, size_t len, bool binary = false);
  void EnqueueMessage(const char* data, size_t len, bool binary = false) {
    EnqueueMessage(reinterpret_cast<const uint8_t*>(data), len, binary);
  }
  void Close();
  bool OnWritable();

 private:
  struct WsBuffer {
    WsBuffer(std::vector<uint8_t> data, bool binary)
        : data(std::move(data)), binary(binary) {}
    std::vector<uint8_t> data;
    bool binary;
  };

  void WriteWsBuffer(WsBuffer& ws_buffer);

  struct lws* wsi_;
  bool close_ = false;
  std::deque<WsBuffer> buffer_queue_;
};

class WebSocketHandlerFactory {
 public:
  virtual ~WebSocketHandlerFactory() = default;
  virtual std::shared_ptr<WebSocketHandler> Build(struct lws* wsi) = 0;
};

}  // namespace cuttlefish
