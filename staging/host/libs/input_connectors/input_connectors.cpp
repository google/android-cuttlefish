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

#include "host/libs/input_connectors/input_connectors.h"

#include <linux/input.h>

#include <vector>

#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"

namespace cuttlefish {
namespace {

class InputConnectorBase {
 public:
  InputConnectorBase(SharedFD server) : server_(server) {}
  virtual ~InputConnectorBase() = default;

  bool AcceptConnection();

  InputConnectorBase() = default;

  bool SendEvents(const void* event_data, size_t size);

 private:
  SharedFD server_;
  SharedFD client_;
};

bool InputConnectorBase::SendEvents(const void* event_data, size_t size) {
  auto sent = WriteAll(client_, reinterpret_cast<const char*>(event_data),
                       size) == size;
  if (!sent) {
    LOG(ERROR) << "Failed to send input events: " << client_->StrError();
  }
  return sent;
}

bool InputConnectorBase::AcceptConnection() {
  client_ = SharedFD::Accept(*server_);
  return client_->IsOpen();
}

////////////////////////////////////////////////////////////////////////////////

template <typename T>
struct InputEventBuffer {
  InputEventBuffer() {
    buffer_.reserve(6);  // 6 is usually enough
  }

  void AddEvent(uint16_t type, uint16_t code, int32_t value) {
    buffer_.push_back({.type = type, .code = code, .value = value});
  }

  T* data() { return buffer_.data(); }

  const T* data() const { return buffer_.data(); }

  std::size_t byte_size() const { return buffer_.size() * sizeof(T); }

 private:
  std::vector<T> buffer_;
};

////////////////////////////////////////////////////////////////////////////////

template <typename T>
class KeyboardConnectorImpl : public KeyboardConnector {
 public:
  KeyboardConnectorImpl(SharedFD server) : base_(server) {}
  ~KeyboardConnectorImpl() override = default;

  void InjectKeyEvent(uint16_t code, bool down) {
    InputEventBuffer<T> buffer;
    buffer.AddEvent(EV_KEY, code, down);
    buffer.AddEvent(EV_SYN, 0, 0);
    base_.SendEvents(buffer.data(), buffer.byte_size());
  }

  bool AcceptConnection() { return base_.AcceptConnection(); }

 private:
  InputConnectorBase base_;
};

template <typename T>
class TouchConnectorImpl : public TouchConnector {
 public:
  TouchConnectorImpl(SharedFD server) : base_(server) {}
  ~TouchConnectorImpl() override = default;

  void InjectTouchEvent(int32_t x, int32_t y, bool down) {
    InputEventBuffer<T> buffer;
    buffer.AddEvent(EV_ABS, ABS_X, x);
    buffer.AddEvent(EV_ABS, ABS_Y, y);
    buffer.AddEvent(EV_KEY, BTN_TOUCH, down);
    buffer.AddEvent(EV_SYN, 0, 0);
    base_.SendEvents(buffer.data(), buffer.byte_size());
  }

  bool AcceptConnection() { return base_.AcceptConnection(); }

 private:
  InputConnectorBase base_;
};

////////////////////////////////////////////////////////////////////////////////

struct virtio_input_event {
  uint16_t type;
  uint16_t code;
  int32_t value;
};

template <typename Iface, class Impl>
std::unique_ptr<Iface> CreateInputConnector(SharedFD server) {
  std::unique_ptr<Iface> ret(new Impl(server));
  auto accepted = reinterpret_cast<Impl*>(ret.get())->AcceptConnection();
  if (!accepted) {
    return nullptr;
  }
  return ret;
}

}  // namespace

KeyboardConnector::KeyboardConnector() = default;

KeyboardConnector::~KeyboardConnector() = default;

std::unique_ptr<KeyboardConnector> KeyboardConnector::Create(
    SharedFD server, bool use_virtio_events) {
  return use_virtio_events
             ? CreateInputConnector<KeyboardConnector,
                                    KeyboardConnectorImpl<virtio_input_event>>(
                   server)
             : CreateInputConnector<KeyboardConnector,
                                    KeyboardConnectorImpl<input_event>>(server);
}

TouchConnector::TouchConnector() = default;

TouchConnector::~TouchConnector() = default;

std::unique_ptr<TouchConnector> TouchConnector::Create(SharedFD server,
                                                       bool use_virtio_events) {
  return use_virtio_events
             ? CreateInputConnector<TouchConnector,
                                    TouchConnectorImpl<virtio_input_event>>(
                   server)
             : CreateInputConnector<TouchConnector,
                                    TouchConnectorImpl<input_event>>(server);
}

}  // namespace cuttlefish