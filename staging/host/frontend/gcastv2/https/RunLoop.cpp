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

#include <https/RunLoop.h>

#include <https/Support.h>

#include <android-base/logging.h>

#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

#include <mutex>
#include <condition_variable>

bool RunLoop::QueueElem::operator<=(const QueueElem &other) const {
    if (mWhen) {
        if (other.mWhen) {
            return mWhen <= other.mWhen;
        }

        return false;
    }

    if (other.mWhen) {
        return true;
    }

    // This ensures that two events posted without a trigger time are queued in
    // the order they were post()ed in.
    return true;
}

RunLoop::RunLoop()
    : mDone(false),
      mPThread(0),
      mNextToken(1) {
    int res = pipe(mControlFds);
    CHECK_GE(res, 0);

    makeFdNonblocking(mControlFds[0]);
}

RunLoop::RunLoop(std::string_view name)
    : RunLoop() {
    mName = name;

    mThread = std::thread([this]{ run(); });
}

RunLoop::~RunLoop() {
    stop();

    close(mControlFds[1]);
    mControlFds[1] = -1;

    close(mControlFds[0]);
    mControlFds[0] = -1;
}

void RunLoop::stop() {
    mDone = true;
    interrupt();

    if (mThread.joinable()) {
        mThread.join();
    }
}

RunLoop::Token RunLoop::post(AsyncFunction fn) {
    CHECK(fn != nullptr);

    auto token = mNextToken++;
    insert({ std::nullopt, fn, token });

    return token;
}

bool RunLoop::postAndAwait(AsyncFunction fn) {
    if (isCurrentThread()) {
        // To wait from the runloop's thread would cause deadlock
        post(fn);
        return false;
    }

    std::mutex mtx;
    bool ran = false;
    std::condition_variable cond_var;

    post([&cond_var, &mtx, &ran, fn](){
        fn();
        {
            std::unique_lock<std::mutex> lock(mtx);
            ran = true;
            // Notify while holding the mutex, otherwise the condition variable
            // could be destroyed before the call to notify_all.
            cond_var.notify_all();
        }
    });

    {
        std::unique_lock<std::mutex> lock(mtx);
        cond_var.wait(lock, [&ran](){ return ran;});
    }
    return ran;
}

RunLoop::Token RunLoop::postWithDelay(
        std::chrono::steady_clock::duration delay, AsyncFunction fn) {
    CHECK(fn != nullptr);

    auto token = mNextToken++;
    insert({ std::chrono::steady_clock::now() + delay, fn, token });

    return token;
}

bool RunLoop::cancelToken(Token token) {
    std::lock_guard<std::mutex> autoLock(mLock);

    bool found = false;
    for (auto it = mQueue.begin(); it != mQueue.end(); ++it) {
        if (it->mToken == token) {
            mQueue.erase(it);

            if (it == mQueue.begin()) {
                interrupt();
            }

            found = true;
            break;
        }
    }

    return found;
}

void RunLoop::postSocketRecv(int sock, AsyncFunction fn) {
    CHECK_GE(sock, 0);
    CHECK(fn != nullptr);

    std::lock_guard<std::mutex> autoLock(mLock);
    mAddInfos.push_back({ sock, InfoType::RECV, fn });
    interrupt();
}

void RunLoop::postSocketSend(int sock, AsyncFunction fn) {
    CHECK_GE(sock, 0);
    CHECK(fn != nullptr);

    std::lock_guard<std::mutex> autoLock(mLock);
    mAddInfos.push_back({ sock, InfoType::SEND, fn });
    interrupt();
}

void RunLoop::cancelSocket(int sock) {
    CHECK_GE(sock, 0);

    std::lock_guard<std::mutex> autoLock(mLock);
    mAddInfos.push_back({ sock, InfoType::CANCEL, nullptr });
    interrupt();
}

void RunLoop::insert(const QueueElem &elem) {
    std::lock_guard<std::mutex> autoLock(mLock);

    auto it = mQueue.begin();
    while (it != mQueue.end() && *it <= elem) {
        ++it;
    }

    if (it == mQueue.begin()) {
        interrupt();
    }

    mQueue.insert(it, elem);
}

void RunLoop::run() {
    mPThread = pthread_self();

    std::map<int, SocketCallbacks> socketCallbacksByFd;
    std::vector<pollfd> pollFds;

    auto removePollFdAt = [&socketCallbacksByFd, &pollFds](size_t i) {
        if (i + 1 == pollFds.size()) {
            pollFds.pop_back();
        } else {
            // Instead of leaving a hole in the middle of the
            // pollFds vector, we copy the last item into
            // that hole and reduce the size of the vector by 1,
            // taking are of updating the corresponding callback
            // with the correct, new index.
            pollFds[i] = pollFds.back();
            pollFds.pop_back();
            socketCallbacksByFd[pollFds[i].fd].mPollFdIndex = i;
        }
    };

    // The control channel's pollFd will always be at index 0.
    pollFds.push_back({ mControlFds[0], POLLIN, 0 });

    for (;;) {
        int timeoutMs = -1;  // wait Forever

        {
            std::lock_guard<std::mutex> autoLock(mLock);

            if (mDone) {
                break;
            }

            for (const auto &addInfo : mAddInfos) {
                const int sock = addInfo.mSock;
                const auto fn = addInfo.mFn;

                auto it = socketCallbacksByFd.find(sock);

                switch (addInfo.mType) {
                    case InfoType::RECV:
                    {
                        if (it == socketCallbacksByFd.end()) {
                            socketCallbacksByFd[sock] = { fn, nullptr, pollFds.size() };
                            pollFds.push_back({ sock, POLLIN, 0 });
                        } else {
                            // There's already a pollFd for this socket.
                            CHECK(it->second.mSendFn != nullptr);

                            CHECK(it->second.mRecvFn == nullptr);
                            it->second.mRecvFn = fn;

                            pollFds[it->second.mPollFdIndex].events |= POLLIN;
                        }
                        break;
                    }

                    case InfoType::SEND:
                    {
                        if (it == socketCallbacksByFd.end()) {
                            socketCallbacksByFd[sock] = { nullptr, fn, pollFds.size() };
                            pollFds.push_back({ sock, POLLOUT, 0 });
                        } else {
                            // There's already a pollFd for this socket.
                            if (it->second.mRecvFn == nullptr) {
                                LOG(ERROR)
                                    << "There's an entry but no recvFn "
                                       "notification for socket "
                                    << sock;
                            }

                            CHECK(it->second.mRecvFn != nullptr);

                            if (it->second.mSendFn != nullptr) {
                                LOG(ERROR)
                                    << "There's already a pending send "
                                       "notification for socket "
                                    << sock;
                            }
                            CHECK(it->second.mSendFn == nullptr);
                            it->second.mSendFn = fn;

                            pollFds[it->second.mPollFdIndex].events |= POLLOUT;
                        }
                        break;
                    }

                    case InfoType::CANCEL:
                    {
                        if (it != socketCallbacksByFd.end()) {
                            const size_t i = it->second.mPollFdIndex;

                            socketCallbacksByFd.erase(it);
                            removePollFdAt(i);
                        }
                        break;
                    }
                }
            }

            mAddInfos.clear();

            if (!mQueue.empty()) {
                timeoutMs = 0;

                if (mQueue.front().mWhen) {
                    auto duration =
                        *mQueue.front().mWhen - std::chrono::steady_clock::now();

                    auto durationMs =
                        std::chrono::duration_cast<std::chrono::milliseconds>(duration);

                    if (durationMs.count() > 0) {
                        timeoutMs = static_cast<int>(durationMs.count());
                    }
                }
            }
        }

        int pollRes = 0;
        if (timeoutMs != 0) {
            // NOTE: The inequality is on purpose, we'll want to execute this
            // code if timeoutMs == -1 (infinite) or timeoutMs > 0, but not
            // if it's 0.

            pollRes = poll(
                    pollFds.data(),
                    static_cast<nfds_t>(pollFds.size()),
                    timeoutMs);
        }

        if (pollRes < 0) {
            if (errno != EINTR) {
                std::cerr
                    << "poll FAILED w/ "
                    << errno
                    << " ("
                    << strerror(errno)
                    << ")"
                    << std::endl;
            }

            CHECK_EQ(errno, EINTR);
            continue;
        }

        std::vector<AsyncFunction> fnArray;

        {
            std::lock_guard<std::mutex> autoLock(mLock);

            if (pollRes > 0) {
                if (pollFds[0].revents & POLLIN) {
                    ssize_t res;
                    do {
                        uint8_t c[32];
                        while ((res = read(mControlFds[0], c, sizeof(c))) < 0
                                && errno == EINTR) {
                        }
                    } while (res > 0);
                    CHECK(res < 0 && errno == EWOULDBLOCK);

                    --pollRes;
                }

                // NOTE: Skip index 0, as we already handled it above.
                // Also, bail early if we exhausted all actionable pollFds
                // according to pollRes.
                for (size_t i = pollFds.size(); pollRes && i-- > 1;) {
                    pollfd &pollFd = pollFds[i];
                    const short revents = pollFd.revents;

                    if (revents) {
                        --pollRes;
                    }

                    const bool readable = (revents & POLLIN);
                    const bool writable = (revents & POLLOUT);
                    const bool dead = (revents & POLLNVAL);

                    bool removeCallback = dead;

                    if (readable || writable || dead) {
                        const int sock = pollFd.fd;

                        const auto &it = socketCallbacksByFd.find(sock);
                        auto &cb = it->second;
                        CHECK_EQ(cb.mPollFdIndex, i);

                        if (readable) {
                            CHECK(cb.mRecvFn != nullptr);
                            fnArray.push_back(cb.mRecvFn);
                            cb.mRecvFn = nullptr;
                            pollFd.events &= ~POLLIN;

                            removeCallback |= (cb.mSendFn == nullptr);
                        }

                        if (writable) {
                            CHECK(cb.mSendFn != nullptr);
                            fnArray.push_back(cb.mSendFn);
                            cb.mSendFn = nullptr;
                            pollFd.events &= ~POLLOUT;

                            removeCallback |= (cb.mRecvFn == nullptr);
                        }

                        if (removeCallback) {
                            socketCallbacksByFd.erase(it);
                            removePollFdAt(i);
                        }
                    }
                }
            } else {
                // No interrupt, no socket notifications.
                fnArray.push_back(mQueue.front().mFn);
                mQueue.pop_front();
            }
        }

        for (const auto &fn : fnArray) {
            fn();
        }
    }
}

void RunLoop::interrupt() {
    uint8_t c = 1;
    ssize_t res;
    while ((res = write(mControlFds[1], &c, 1)) < 0 && errno == EINTR) {
    }

    CHECK_EQ(res, 1);
}

struct MainRunLoop : public RunLoop {
};

static std::mutex gLock;
static std::shared_ptr<RunLoop> gMainRunLoop;

// static
std::shared_ptr<RunLoop> RunLoop::main() {
    std::lock_guard<std::mutex> autoLock(gLock);
    if (!gMainRunLoop) {
        gMainRunLoop = std::make_shared<MainRunLoop>();
    }
    return gMainRunLoop;
}

bool RunLoop::isCurrentThread() const {
    return pthread_equal(pthread_self(), mPThread);
}

