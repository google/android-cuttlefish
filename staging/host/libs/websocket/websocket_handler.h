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
#include <string>
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

class WebSocketServer;

enum class HttpStatusCode : int {
  // From https://developer.mozilla.org/en-US/docs/Web/HTTP/Status
  Ok = 200,
  NoContent = 204,
  BadRequest = 400,
  Unauthorized = 401,
  NotFound = 404,
  MethodNotAllowed = 405,
  Conflict = 409,
};

class DynHandler {
 public:
  DynHandler(struct lws* wsi);

  virtual ~DynHandler() = default;
  // TODO (jemoreira): Allow more than just JSON replies
  // TODO (jemoreira): Receive request parameters
  // Handle a GET request.
  virtual HttpStatusCode DoGet() = 0;
  // Handle a POST request.
  virtual HttpStatusCode DoPost() = 0;

 protected:
  void AppendDataOut(const std::string& data);
  const std::string& GetDataIn() const { return in_buffer_; }

 private:
  friend WebSocketServer;
  void AppendDataIn(void* data, size_t len);
  int OnWritable();
  size_t content_len() const;

  struct lws* wsi_;
  std::string in_buffer_ = {};
  std::string out_buffer_ = {};
};

using DynHandlerFactory =
    std::function<std::unique_ptr<DynHandler>(struct lws*)>;
}  // namespace cuttlefish
