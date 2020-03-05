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

#include <source/InputSink.h>

#include <linux/input.h>

#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <android-base/logging.h>
#include <https/SafeCallbackable.h>
#include <https/Support.h>

namespace android {

namespace {

// TODO de-dup this from vnc server and here
struct virtio_input_event {
  uint16_t type;
  uint16_t code;
  int32_t value;
};

template <typename T>
struct InputEventBufferImpl : public InputEventBuffer {
  InputEventBufferImpl() {
    buffer_.reserve(6);  // 6 is usually enough even for multi-touch
  }
  void addEvent(uint16_t type, uint16_t code, int32_t value) override {
    buffer_.push_back({.type = type, .code = code, .value = value});
  }
  T* data() { return buffer_.data(); }
  const void* data() const override { return buffer_.data(); }
  std::size_t size() const override { return buffer_.size() * sizeof(T); }

 private:
  std::vector<T> buffer_;
};

}  // namespace

InputSink::InputSink(std::shared_ptr<RunLoop> runLoop, int serverFd,
                     bool write_virtio_input)
    : mRunLoop(runLoop),
      mServerFd(serverFd),
      mClientFd(-1),
      mSendPending(false),
      mWriteVirtioInput(write_virtio_input) {
  if (mServerFd >= 0) {
    makeFdNonblocking(mServerFd);
  }
}

InputSink::~InputSink() {
  if (mClientFd >= 0) {
    mRunLoop->cancelSocket(mClientFd);

    close(mClientFd);
    mClientFd = -1;
  }

  if (mServerFd >= 0) {
    mRunLoop->cancelSocket(mServerFd);

    close(mServerFd);
    mServerFd = -1;
  }
}

void InputSink::start() {
  if (mServerFd < 0) {
    return;
  }

  mRunLoop->postSocketRecv(
      mServerFd, makeSafeCallback(this, &InputSink::onServerConnection));
}

std::unique_ptr<InputEventBuffer> InputSink::getEventBuffer() const {
  InputEventBuffer* raw_ptr;
  if (mWriteVirtioInput) {
    raw_ptr = new InputEventBufferImpl<virtio_input_event>();
  } else {
    raw_ptr = new InputEventBufferImpl<input_event>();
  }
  return std::unique_ptr<InputEventBuffer>(raw_ptr);
}

void InputSink::SendEvents(std::unique_ptr<InputEventBuffer> evt_buffer) {
  sendRawEvents(evt_buffer->data(), evt_buffer->size());
}

void InputSink::onServerConnection() {
  int s = accept(mServerFd, nullptr, nullptr);

  if (s >= 0) {
    if (mClientFd >= 0) {
      LOG(INFO) << "Rejecting client, we already have one.";

      // We already have a client.
      close(s);
      s = -1;
    } else {
      LOG(INFO) << "Accepted client socket " << s << ".";

      makeFdNonblocking(s);

      mClientFd = s;
      mRunLoop->postSocketRecv(
        mClientFd, makeSafeCallback(this, &InputSink::onSocketRecv));
    }
  }

  mRunLoop->postSocketRecv(
      mServerFd, makeSafeCallback(this, &InputSink::onServerConnection));
}

void InputSink::sendRawEvents(const void* evt_buffer, size_t size) {
  if (size <= 0) return;

  std::lock_guard autoLock(mLock);

  if (mClientFd < 0) {
    return;
  }

  size_t offset = mOutBuffer.size();
  mOutBuffer.resize(offset + size);
  memcpy(mOutBuffer.data() + offset, evt_buffer, size);

  if (!mSendPending) {
    mSendPending = true;

    mRunLoop->postSocketSend(mClientFd,
                             makeSafeCallback(this, &InputSink::onSocketSend));
  }
}

void InputSink::onSocketRecv() {
  if (mClientFd < 0) return;

  char buff[512];
  auto n = recv(mClientFd, buff, sizeof(buff), 0 /* flags */);
  if (n > 0) {
    LOG(INFO) << "Discarding " << n << " bytes received from the input device.";
    mRunLoop->postSocketRecv(
        mClientFd, makeSafeCallback(this, &InputSink::onSocketRecv));
  } else {
    // Client disconnected
    if (n < 0) {
      auto errno_save = errno;
      LOG(ERROR) << "Error receiving from socket: " << strerror(errno_save);
    }
    mRunLoop->cancelSocket(mClientFd);
    close(mClientFd);
    mClientFd = -1;
  }
}

void InputSink::onSocketSend() {
  std::lock_guard autoLock(mLock);

  CHECK(mSendPending);
  mSendPending = false;

  if (mClientFd < 0) {
    return;
  }

  ssize_t n;
  while (!mOutBuffer.empty()) {
    do {
      n = ::send(mClientFd, mOutBuffer.data(), mOutBuffer.size(), 0);
    } while (n < 0 && errno == EINTR);

    if (n <= 0) {
      break;
    }

    mOutBuffer.erase(mOutBuffer.begin(), mOutBuffer.begin() + n);
  }

  if ((n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || n == 0) {
    LOG(ERROR) << "Client is gone.";

    // Client is gone.
    mRunLoop->cancelSocket(mClientFd);

    close(mClientFd);
    mClientFd = -1;
    return;
  }

  if (!mOutBuffer.empty()) {
    mSendPending = true;
    mRunLoop->postSocketSend(mClientFd,
                             makeSafeCallback(this, &InputSink::onSocketSend));
  }
}

}  // namespace android
