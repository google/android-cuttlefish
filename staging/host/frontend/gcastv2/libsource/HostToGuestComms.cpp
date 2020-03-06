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

#include <source/HostToGuestComms.h>

#include <https/SafeCallbackable.h>
#include <https/Support.h>

#include <android-base/logging.h>

HostToGuestComms::HostToGuestComms(
        std::shared_ptr<RunLoop> runLoop,
        bool isServer,
        int fd,
        ReceiveCb onReceive)
    : mRunLoop(runLoop),
      mIsServer(isServer),
      mOnReceive(onReceive),
      mServerSock(-1),
      mSock(-1),
      mInBufferLen(0),
      mSendPending(false),
      mConnected(false) {
    makeFdNonblocking(fd);
    if (mIsServer) {
        mServerSock = fd;
    } else {
        mSock = fd;
    }
}

HostToGuestComms::HostToGuestComms(
        std::shared_ptr<RunLoop> runLoop,
        bool isServer,
        uint32_t cid,
        uint16_t port,
        ReceiveCb onReceive)
    : mRunLoop(runLoop),
      mIsServer(isServer),
      mOnReceive(onReceive),
      mServerSock(-1),
      mSock(-1),
      mInBufferLen(0),
      mSendPending(false),
      mConnected(false) {
    int s = socket(AF_VSOCK, SOCK_STREAM, 0);
    CHECK_GE(s, 0);

    LOG(INFO) << "HostToGuestComms created socket " << s;

    makeFdNonblocking(s);

    sockaddr_vm addr;
    memset(&addr, 0, sizeof(addr));
    addr.svm_family = AF_VSOCK;
    addr.svm_port = port;
    addr.svm_cid = cid;

    int res;
    if (mIsServer) {
        LOG(INFO)
            << "Binding to cid "
            << (addr.svm_cid == VMADDR_CID_ANY)
                    ? "VMADDR_CID_ANY" : std::to_string(addr.svm_cid);

        res = bind(s, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));

        if (res) {
            LOG(ERROR)
                << (mIsServer ? "bind" : "connect")
                << " FAILED w/ errno "
                << errno
                << " ("
                << strerror(errno)
                << ")";
        }

        CHECK(!res);

        res = listen(s, 4);
        CHECK(!res);

        mServerSock = s;
    } else {
        mSock = s;
        mConnectToAddr = addr;
    }
}

HostToGuestComms::~HostToGuestComms() {
    if (mSock >= 0) {
        mRunLoop->cancelSocket(mSock);

        close(mSock);
        mSock = -1;
    }

    if (mServerSock >= 0) {
        mRunLoop->cancelSocket(mServerSock);

        close(mServerSock);
        mServerSock = -1;
    }
}

void HostToGuestComms::start() {
    if (mIsServer) {
        mRunLoop->postSocketRecv(
                mServerSock,
                makeSafeCallback(this, &HostToGuestComms::onServerConnection));
    } else {
        mRunLoop->postWithDelay(
                std::chrono::milliseconds(5000), 
                makeSafeCallback(
                    this,
                    &HostToGuestComms::onAttemptToConnect,
                    mConnectToAddr));
    }
}

void HostToGuestComms::send(const void *data, size_t size, bool addFraming) {
    if (!size) {
        return;
    }

    std::lock_guard autoLock(mLock);

    size_t offset = mOutBuffer.size();

    if (addFraming) {
        uint32_t packetLen = size;
        size_t totalSize = sizeof(packetLen) + size;

        mOutBuffer.resize(offset + totalSize);
        memcpy(mOutBuffer.data() + offset, &packetLen, sizeof(packetLen));
        memcpy(mOutBuffer.data() + offset + sizeof(packetLen), data, size);
    } else {
        mOutBuffer.resize(offset + size);
        memcpy(mOutBuffer.data() + offset, data, size);
    }

    if (mSock >= 0 && (mIsServer || mConnected) && !mSendPending) {
        mSendPending = true;
        mRunLoop->postSocketSend(
                mSock,
                makeSafeCallback(this, &HostToGuestComms::onSocketSend));
    }
}

void HostToGuestComms::onServerConnection() {
    int s = accept(mServerSock, nullptr, nullptr);

    if (s >= 0) {
        if (mSock >= 0) {
            LOG(INFO) << "Rejecting client, we already have one.";

            // We already have a client.
            close(s);
            s = -1;
        } else {
            LOG(INFO) << "Accepted client socket " << s << ".";

            makeFdNonblocking(s);

            mSock = s;
            mRunLoop->postSocketRecv(
                    mSock,
                    makeSafeCallback(this, &HostToGuestComms::onSocketReceive));

            std::lock_guard autoLock(mLock);
            if (!mOutBuffer.empty()) {
                CHECK(!mSendPending);

                mSendPending = true;
                mRunLoop->postSocketSend(
                        mSock,
                        makeSafeCallback(
                            this, &HostToGuestComms::onSocketSend));
            }
        }
    }

    mRunLoop->postSocketRecv(
            mServerSock,
            makeSafeCallback(this, &HostToGuestComms::onServerConnection));
}

void HostToGuestComms::onSocketReceive() {
    ssize_t n;
    for (;;) {
        static constexpr size_t kChunkSize = 65536;

        mInBuffer.resize(mInBufferLen + kChunkSize);

        do {
            n = recv(mSock, mInBuffer.data() + mInBufferLen, kChunkSize, 0);
        } while (n < 0 && errno == EINTR);

        if (n <= 0) {
            break;
        }

        mInBufferLen += static_cast<size_t>(n);
    }

    int savedErrno = errno;

    drainInBuffer();

    if ((n < 0 && savedErrno != EAGAIN && savedErrno != EWOULDBLOCK)
            || n == 0) {
        LOG(ERROR) << "Client is gone.";

        // Client is gone.
        mRunLoop->cancelSocket(mSock);

        mSendPending = false;

        close(mSock);
        mSock = -1;
        return;
    }

    mRunLoop->postSocketRecv(
            mSock,
            makeSafeCallback(this, &HostToGuestComms::onSocketReceive));
}

void HostToGuestComms::drainInBuffer() {
    for (;;) {
        uint32_t packetLen;

        if (mInBufferLen < sizeof(packetLen)) {
            return;
        }

        memcpy(&packetLen, mInBuffer.data(), sizeof(packetLen));

        size_t totalLen = sizeof(packetLen) + packetLen;

        if (mInBufferLen < totalLen) {
            return;
        }

        if (mOnReceive) {
            LOG(VERBOSE) << "Dispatching packet of size " << packetLen;

            mOnReceive(mInBuffer.data() + sizeof(packetLen), packetLen);
        }

        mInBuffer.erase(mInBuffer.begin(), mInBuffer.begin() + totalLen);
        mInBufferLen -= totalLen;
    }
}

void HostToGuestComms::onSocketSend() {
    std::lock_guard autoLock(mLock);

    CHECK(mSendPending);
    mSendPending = false;

    if (mSock < 0) {
        return;
    }

    ssize_t n;
    while (!mOutBuffer.empty()) {
        do {
            n = ::send(mSock, mOutBuffer.data(), mOutBuffer.size(), 0);
        } while (n < 0 && errno == EINTR);

        if (n <= 0) {
            break;
        }

        mOutBuffer.erase(mOutBuffer.begin(), mOutBuffer.begin() + n);
    }

    if ((n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || n == 0) {
        LOG(ERROR) << "Client is gone.";

        // Client is gone.
        mRunLoop->cancelSocket(mSock);

        close(mSock);
        mSock = -1;
        return;
    }

    if (!mOutBuffer.empty()) {
        mSendPending = true;
        mRunLoop->postSocketSend(
                mSock,
                makeSafeCallback(this, &HostToGuestComms::onSocketSend));
    }
}

void HostToGuestComms::onAttemptToConnect(const sockaddr_vm &addr) {
    LOG(VERBOSE) << "Attempting to connect to cid " << addr.svm_cid;

    int res;
    do {
        res = connect(
            mSock, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
    } while (res < 0 && errno == EINTR);

    if (res < 0) {
        if (errno == EINPROGRESS) {
            LOG(VERBOSE) << "EINPROGRESS, waiting to check the connection.";

            mRunLoop->postSocketSend(
                    mSock,
                    makeSafeCallback(
                        this, &HostToGuestComms::onCheckConnection, addr));

            return;
        }

        LOG(INFO)
            << "Our attempt to connect to the guest FAILED w/ error "
            << errno
            << " ("
            << strerror(errno)
            << "), will try again shortly.";

        mRunLoop->postWithDelay(
                std::chrono::milliseconds(5000),
                makeSafeCallback(
                    this, &HostToGuestComms::onAttemptToConnect, addr));

        return;
    }

    onConnected();
}

void HostToGuestComms::onCheckConnection(const sockaddr_vm &addr) {
    int err;

    int res;
    do {
        socklen_t errSize = sizeof(err);

        res = getsockopt(mSock, SOL_SOCKET, SO_ERROR, &err, &errSize);
    } while (res < 0 && errno == EINTR);

    CHECK(!res);

    if (!err) {
        onConnected();
    } else {
        LOG(VERBOSE)
            << "Connection failed w/ error "
            << err
            << " ("
            << strerror(err)
            << "), will try again shortly.";

        // Is there a better way of cancelling the (failed) connection that
        // somehow is still in progress on the socket and restarting it?
        mRunLoop->cancelSocket(mSock);

        close(mSock);
        mSock = socket(AF_VSOCK, SOCK_STREAM, 0);
        CHECK_GE(mSock, 0);

        makeFdNonblocking(mSock);

        mRunLoop->postWithDelay(
                std::chrono::milliseconds(5000),
                makeSafeCallback(
                    this, &HostToGuestComms::onAttemptToConnect, addr));
    }
}

void HostToGuestComms::onConnected() {
    LOG(INFO) << "Connected to guest.";

    std::lock_guard autoLock(mLock);

    mConnected = true;
    CHECK(!mSendPending);

    if (!mOutBuffer.empty()) {
        mSendPending = true;
        mRunLoop->postSocketSend(
                mSock,
                makeSafeCallback(this, &HostToGuestComms::onSocketSend));
    }

    mRunLoop->postSocketRecv(
            mSock,
            makeSafeCallback(this, &HostToGuestComms::onSocketReceive));
}

