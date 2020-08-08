/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "host/frontend/webrtc/connection_observer.h"

#include <linux/input.h>

#include <thread>
#include <vector>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_buf.h"
#include "host/frontend/webrtc/adb_handler.h"
#include "host/libs/config/cuttlefish_config.h"

DECLARE_bool(write_virtio_input);

namespace cuttlefish {

// TODO (b/147511234): de-dup this from vnc server and here
struct virtio_input_event {
  uint16_t type;
  uint16_t code;
  int32_t value;
};

struct InputEventBuffer {
  virtual ~InputEventBuffer() = default;
  virtual void AddEvent(uint16_t type, uint16_t code, int32_t value) = 0;
  virtual size_t size() const = 0;
  virtual const void *data() const = 0;
};

template <typename T>
struct InputEventBufferImpl : public InputEventBuffer {
  InputEventBufferImpl() {
    buffer_.reserve(6);  // 6 is usually enough
  }
  void AddEvent(uint16_t type, uint16_t code, int32_t value) override {
    buffer_.push_back({.type = type, .code = code, .value = value});
  }
  T *data() { return buffer_.data(); }
  const void *data() const override { return buffer_.data(); }
  std::size_t size() const override { return buffer_.size() * sizeof(T); }

 private:
  std::vector<T> buffer_;
};

std::unique_ptr<InputEventBuffer> GetEventBuffer() {
  if (FLAGS_write_virtio_input) {
    return std::unique_ptr<InputEventBuffer>(
        new InputEventBufferImpl<virtio_input_event>());
  } else {
    return std::unique_ptr<InputEventBuffer>(
        new InputEventBufferImpl<input_event>());
  }
}

class ConnectionObserverImpl
    : public cuttlefish::webrtc_streaming::ConnectionObserver {
 public:
  ConnectionObserverImpl(cuttlefish::SharedFD touch_fd,
                         cuttlefish::SharedFD keyboard_fd,
                         std::weak_ptr<DisplayHandler> display_handler,
                         std::shared_ptr<RunLoop> run_loop)
      : touch_client_(touch_fd),
        keyboard_client_(keyboard_fd),
        weak_display_handler_(display_handler),
        run_loop_(run_loop) {}
  virtual ~ConnectionObserverImpl() = default;

  void OnConnected(std::function<void(const uint8_t *, size_t, bool)>
                   /*ctrl_msg_sender*/) override {
    auto display_handler = weak_display_handler_.lock();
    if (display_handler) {
      // A long time may pass before the next frame comes up from the guest.
      // Send the last one to avoid showing a black screen to the user during
      // that time.
      display_handler->SendLastFrame();
    }
  }
  void OnTouchEvent(const std::string & /*display_label*/, int x, int y,
                    bool down) override {
    auto buffer = GetEventBuffer();
    if (!buffer) {
      LOG(ERROR) << "Failed to allocate event buffer";
      return;
    }
    buffer->AddEvent(EV_ABS, ABS_X, x);
    buffer->AddEvent(EV_ABS, ABS_Y, y);
    buffer->AddEvent(EV_KEY, BTN_TOUCH, down);
    buffer->AddEvent(EV_SYN, 0, 0);
    cuttlefish::WriteAll(touch_client_,
                         reinterpret_cast<const char *>(buffer->data()),
                         buffer->size());
  }
  void OnMultiTouchEvent(const std::string &display_label, int /*id*/,
                         int /*slot*/, int x, int y,
                         bool initialDown) override {
    OnTouchEvent(display_label, x, y, initialDown);
  }
  void OnKeyboardEvent(uint16_t code, bool down) override {
    auto buffer = GetEventBuffer();
    if (!buffer) {
      LOG(ERROR) << "Failed to allocate event buffer";
      return;
    }
    buffer->AddEvent(EV_KEY, code, down);
    buffer->AddEvent(EV_SYN, 0, 0);
    cuttlefish::WriteAll(keyboard_client_,
                         reinterpret_cast<const char *>(buffer->data()),
                         buffer->size());
  }
  void OnAdbChannelOpen(std::function<bool(const uint8_t *, size_t)>
                            adb_message_sender) override {
    LOG(VERBOSE) << "Adb Channel open";
    adb_handler_.reset(new cuttlefish::webrtc_streaming::AdbHandler(
        run_loop_,
        cuttlefish::CuttlefishConfig::Get()
            ->ForDefaultInstance()
            .adb_ip_and_port(),
        adb_message_sender));
    adb_handler_->run();
  }
  void OnAdbMessage(const uint8_t *msg, size_t size) override {
    adb_handler_->handleMessage(msg, size);
  }
  void OnControlMessage(const uint8_t */*msg*/, size_t /*size*/) override {
    // TODO (b/163078987): Respond to control commands from the clients
  }

 private:
  cuttlefish::SharedFD touch_client_;
  cuttlefish::SharedFD keyboard_client_;
  std::shared_ptr<cuttlefish::webrtc_streaming::AdbHandler> adb_handler_;
  std::weak_ptr<DisplayHandler> weak_display_handler_;
  std::shared_ptr<RunLoop> run_loop_;
};

CfConnectionObserverFactory::CfConnectionObserverFactory(
    cuttlefish::SharedFD touch_fd, cuttlefish::SharedFD keyboard_fd)
    : touch_fd_(touch_fd),
      keyboard_fd_(keyboard_fd),
      run_loop_(RunLoop::main()),
      run_loop_thread_([this]() { run_loop_->run(); }) {}

std::shared_ptr<cuttlefish::webrtc_streaming::ConnectionObserver>
CfConnectionObserverFactory::CreateObserver() {
  return std::shared_ptr<cuttlefish::webrtc_streaming::ConnectionObserver>(
      new ConnectionObserverImpl(touch_fd_, keyboard_fd_, weak_display_handler_,
                                 run_loop_));
}

void CfConnectionObserverFactory::SetDisplayHandler(
    std::weak_ptr<DisplayHandler> display_handler) {
  weak_display_handler_ = display_handler;
}
}  // namespace cuttlefish
