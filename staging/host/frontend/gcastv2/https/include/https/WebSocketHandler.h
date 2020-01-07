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

#include <arpa/inet.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <sys/types.h>

struct ClientSocket;

struct WebSocketHandler {
    virtual ~WebSocketHandler() = default;

    // Returns number bytes processed or error.
    ssize_t handleRequest(uint8_t *data, size_t size, bool isEOS);

    bool isConnected();

    virtual void setClientSocket(std::weak_ptr<ClientSocket> client);

    typedef std::function<void(const uint8_t *, size_t)> OutputCallback;
    void setOutputCallback(const sockaddr_in &remoteAddr, OutputCallback fn);

    enum class SendMode {
        text,
        binary,
        closeConnection,
    };
    int sendMessage(
            const void *data, size_t size, SendMode mode = SendMode::text);

    std::string remoteHost() const;

protected:
    virtual int handleMessage(
            uint8_t headerByte, const uint8_t *msg, size_t len) = 0;

private:
    std::weak_ptr<ClientSocket> mClientSocket;

    OutputCallback mOutputCallback;
    sockaddr_in mRemoteAddr;
};
