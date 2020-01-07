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

#include <https/PlainSocket.h>

#include <sys/socket.h>

PlainSocket::PlainSocket(std::shared_ptr<RunLoop> rl, int sock)
    : BufferedSocket(rl, sock) {
}

void PlainSocket::postRecv(RunLoop::AsyncFunction fn) {
    runLoop()->postSocketRecv(fd(), fn);
}

void PlainSocket::postSend(RunLoop::AsyncFunction fn) {
    runLoop()->postSocketSend(fd(), fn);
}

ssize_t PlainSocket::recvfrom(
        void *data,
        size_t size,
        sockaddr *address,
        socklen_t *addressLen) {
    return ::recvfrom(fd(), data, size, 0, address, addressLen);
}

ssize_t PlainSocket::sendto(
        const void *data,
        size_t size,
        const sockaddr *addr,
        socklen_t addrLen) {
    if (!addr) {
        return ::send(fd(), data, size, 0);
    }
    return ::sendto(fd(), data, size, 0, addr, addrLen);
}

void PlainSocket::postFlush(RunLoop::AsyncFunction fn) {
    fn();
}
