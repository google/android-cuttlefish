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

#include <https/BufferedSocket.h>

#include <https/WebSocketHandler.h>

#include <arpa/inet.h>
#include <vector>
#include <memory>

#include <https/PlainSocket.h>
#include <https/SSLSocket.h>

struct HTTPServer;
struct RunLoop;
struct ServerSocket;

struct ClientSocket : public std::enable_shared_from_this<ClientSocket> {
    explicit ClientSocket(
            std::shared_ptr<RunLoop> rl,
            HTTPServer *server,
            ServerSocket *parent,
            const sockaddr_in &addr,
            int sock);

    ClientSocket(const ClientSocket &) = delete;
    ClientSocket &operator=(const ClientSocket &) = delete;

    void run();

    int fd() const;

    void queueResponse(const std::string &response, const std::string &body);
    void setWebSocketHandler(std::shared_ptr<WebSocketHandler> handler);

    void queueOutputData(const uint8_t *data, size_t size);

    sockaddr_in remoteAddr() const;

private:
    std::shared_ptr<RunLoop> mRunLoop;
    HTTPServer *mServer;
    ServerSocket *mParent;
    sockaddr_in mRemoteAddr;

    std::shared_ptr<BufferedSocket> mImplPlain;
    std::shared_ptr<SSLSocket> mImplSSL;

    std::vector<uint8_t> mInBuffer;
    size_t mInBufferLen;

    std::vector<uint8_t> mOutBuffer;
    bool mSendPending;

    bool mDisconnecting;

    std::shared_ptr<WebSocketHandler> mWebSocketHandler;

    void handleIncomingData();

    // Returns true iff the client should close the connection.
    bool handleRequest(bool isEOS);

    void sendOutputData();

    void disconnect();
    void finishDisconnect();

    BufferedSocket *getImpl() const {
        return mImplSSL ? mImplSSL.get() : mImplPlain.get();
    }
};
