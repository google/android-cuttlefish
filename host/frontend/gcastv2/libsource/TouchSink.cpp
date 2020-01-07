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

#include <source/TouchSink.h>

#include <https/SafeCallbackable.h>
#include <https/Support.h>

#include <android-base/logging.h>

#include <linux/input.h>
#include <linux/uinput.h>

#include <sys/socket.h>
#include <unistd.h>

namespace android {

namespace {
// TODO de-dup this from vnc server and here
struct virtio_input_event {
  uint16_t type;
  uint16_t code;
  int32_t value;
};

template <typename T>
void SendEvent(int32_t x, int32_t y, int32_t down,
               std::function<void(const void*, size_t)> sender) {
  std::vector<T> events = {{.type = EV_ABS, .code = ABS_X, .value = x},
                           {.type = EV_ABS, .code = ABS_Y, .value = y},
                           {.type = EV_KEY, .code = BTN_TOUCH, .value = down},
                           {.type = EV_SYN, .code = 0, .value = 0}};
  sender(events.data(), events.size() * sizeof(T));
}

template <typename T>
void SendMTEvent(int32_t /* id */, int32_t x, int32_t y, int32_t initial_down,
                 int32_t /* slot */,
                 std::function<void(const void*, size_t)> sender) {
  // TODO(b/124121375): multitouch
  SendEvent<T>(x, y, initial_down, sender);
}
}

TouchSink::TouchSink(std::shared_ptr<RunLoop> runLoop, int serverFd,
                     bool write_virtio_input)
    : mRunLoop(runLoop),
      mServerFd(serverFd),
      mClientFd(-1),
      mSendPending(false) {
  if (mServerFd >= 0) {
    makeFdNonblocking(mServerFd);
  }
  if (write_virtio_input) {
    send_event_ = [this](int32_t x, int32_t y, bool down) {
      SendEvent<virtio_input_event>(
          x, y, down, [this](const void* b, size_t l) { sendRawEvents(b, l); });
    };
    send_mt_event_ = [this](int32_t id, int32_t x, int32_t y, bool initialDown,
                            int32_t slot) {
      SendMTEvent<virtio_input_event>(
          id, x, y, initialDown, slot,
          [this](const void* b, size_t l) { sendRawEvents(b, l); });
    };
  } else {
    send_event_ = [this](int32_t x, int32_t y, bool down) {
      SendEvent<input_event>(
          x, y, down, [this](const void* b, size_t l) { sendRawEvents(b, l); });
    };
    send_mt_event_ = [this](int32_t id, int32_t x, int32_t y, bool initialDown,
                            int32_t slot) {
      SendMTEvent<input_event>(
          id, x, y, initialDown, slot,
          [this](const void* b, size_t l) { sendRawEvents(b, l); });
    };
  }
}

TouchSink::~TouchSink() {
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

void TouchSink::start() {
    if (mServerFd < 0) {
        return;
    }

    mRunLoop->postSocketRecv(
            mServerFd,
            makeSafeCallback(this, &TouchSink::onServerConnection));
}

void TouchSink::onServerConnection() {
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
        }
    }

    mRunLoop->postSocketRecv(
            mServerFd,
            makeSafeCallback(this, &TouchSink::onServerConnection));
}


void TouchSink::onAccessUnit(const std::shared_ptr<InputEvent> &accessUnit) {
    bool down = accessUnit->down != 0;
    int x = accessUnit->x;
    int y = accessUnit->y;

    LOG(VERBOSE)
        << "Received touch (down="
        << down
        << ", x="
        << x
        << ", y="
        << y;

    send_event_(x, y, down);
}

void TouchSink::sendRawEvents(const void* evt_buffer, size_t size) {
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

        mRunLoop->postSocketSend(
                mClientFd,
                makeSafeCallback(this, &TouchSink::onSocketSend));
    }
}

void TouchSink::onSocketSend() {
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
        mRunLoop->postSocketSend(
                mClientFd,
                makeSafeCallback(this, &TouchSink::onSocketSend));
    }
}

}  // namespace android

