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

#include "host/libs/websocket/websocket_handler.h"

#include <android-base/logging.h>
#include <libwebsockets.h>

#include "host/libs/websocket/websocket_server.h"

namespace cuttlefish {

namespace {
void AppendData(const char* data, size_t len, std::string& buffer) {
  auto ptr = reinterpret_cast<const uint8_t*>(data);
  buffer.reserve(buffer.size() + len);
  buffer.insert(buffer.end(), ptr, ptr + len);
}
}  // namespace

WebSocketHandler::WebSocketHandler(struct lws* wsi) : wsi_(wsi) {}

void WebSocketHandler::EnqueueMessage(const uint8_t* data, size_t len,
                                      bool binary) {
  std::vector<uint8_t> buffer(LWS_PRE + len, 0);
  std::copy(data, data + len, buffer.begin() + LWS_PRE);
  buffer_queue_.emplace_front(std::move(buffer), binary);
  lws_callback_on_writable(wsi_);
}

// Attempts to write what's left on a websocket buffer to the websocket,
// updating the buffer.
void WebSocketHandler::WriteWsBuffer(WebSocketHandler::WsBuffer& ws_buffer) {
  auto len = ws_buffer.data.size() - LWS_PRE;
  // For http2 there must be LWS_PRE bytes at the end as well.
  ws_buffer.data.resize(ws_buffer.data.size() + LWS_PRE);
  auto flags = lws_write_ws_flags(
      ws_buffer.binary ? LWS_WRITE_BINARY : LWS_WRITE_TEXT, true, true);
  auto res = lws_write(wsi_, &ws_buffer.data[LWS_PRE], len,
                       static_cast<enum lws_write_protocol>(flags));
  // lws_write will write all bytes of the provided buffer or enqueue the ones
  // it couldn't write for later, but it guarantees it will consume the entire
  // buffer, so we only need to check for error.
  if (res < 0) {
    // This shouldn't happen since this function is called in response to a
    // LWS_CALLBACK_SERVER_WRITEABLE call.
    LOG(ERROR) << "Failed to write data on the websocket";
  }
}

bool WebSocketHandler::OnWritable() {
  if (buffer_queue_.empty()) {
    return close_;
  }
  WriteWsBuffer(buffer_queue_.back());
  buffer_queue_.pop_back();

  if (!buffer_queue_.empty()) {
    lws_callback_on_writable(wsi_);
  }
  // Only close if there are no more queued writes
  return buffer_queue_.empty() && close_;
}

void WebSocketHandler::Close() {
  close_ = true;
  lws_callback_on_writable(wsi_);
}

DynHandler::DynHandler(struct lws* wsi) : wsi_(wsi), out_buffer_(LWS_PRE, 0) {}

void DynHandler::AppendDataOut(const std::string& data) {
  AppendData(data.c_str(), data.size(), out_buffer_);
}

void DynHandler::AppendDataIn(void* data, size_t len) {
  AppendData(reinterpret_cast<char*>(data), len, in_buffer_);
}

int DynHandler::OnWritable() {
  auto len = out_buffer_.size() - LWS_PRE;
  // For http2 there must be LWS_PRE bytes at the end as well.
  out_buffer_.resize(out_buffer_.size() + LWS_PRE);
  auto res = lws_write(wsi_, reinterpret_cast<uint8_t*>(&out_buffer_[LWS_PRE]),
                       len, LWS_WRITE_HTTP_FINAL);
  if (res != len) {
    // This shouldn't happen since this function is called in response to a
    // LWS_CALLBACK_SERVER_WRITEABLE call.
    LOG(ERROR) << "Failed to write HTTP response";
  }
  return lws_http_transaction_completed(wsi_);
}
size_t DynHandler::content_len() const { return out_buffer_.size() - LWS_PRE; }
}  // namespace cuttlefish
