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

#include <https/BufferedSocket.h>

struct PlainSocket : public BufferedSocket {
    explicit PlainSocket(std::shared_ptr<RunLoop> rl, int sock);

    PlainSocket(const PlainSocket &) = delete;
    PlainSocket &operator=(const PlainSocket &) = delete;

    void postRecv(RunLoop::AsyncFunction fn) override;
    void postSend(RunLoop::AsyncFunction fn) override;

    ssize_t recvfrom(
            void *data,
            size_t size,
            sockaddr *address,
            socklen_t *addressLen) override;

    ssize_t sendto(
            const void *data,
            size_t size,
            const sockaddr *addr,
            socklen_t addrLen) override;

    void postFlush(RunLoop::AsyncFunction fn) override;
};
