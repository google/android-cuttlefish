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

#include <https/ServerSocket.h>
#include <https/WebSocketHandler.h>

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

struct HTTPRequest;

struct HTTPServer {
    explicit HTTPServer(
            std::shared_ptr<RunLoop> runLoop,
            const char *iface = nullptr, // defaults to 0.0.0.0, i.e. INADDR_ANY
            uint16_t port = 8443,
            ServerSocket::TransportType transportType =
                    ServerSocket::TransportType::TLS,
            const std::optional<std::string> &certificate_pem_path =
                    std::nullopt,
            const std::optional<std::string> &private_key_pem_path =
                    std::nullopt);

    HTTPServer(const HTTPServer &) = delete;
    HTTPServer &operator=(const HTTPServer &) = delete;

    uint16_t getLocalPort() const;

    void run();

    // Returns true iff the client should close the connection.
    bool handleSingleRequest(
            ClientSocket *client, const uint8_t *data, size_t size, bool isEOS);

    void addStaticFile(
            const char *at,
            const char *path,
            std::optional<std::string> mimeType = std::nullopt);

    void addStaticContent(
            const char *at,
            const void *data,
            size_t size,
            std::optional<std::string> mimeType = std::nullopt);

    using WebSocketHandlerFactory =
        std::function<std::pair<int32_t, std::shared_ptr<WebSocketHandler>>()>;

    void addWebSocketHandlerFactory(
            const char *at, WebSocketHandlerFactory factory);

    std::optional<std::string> certificate_pem_path() const;
    std::optional<std::string> private_key_pem_path() const;

private:
    struct StaticFileInfo {
        std::variant<std::string, std::vector<uint8_t>> mPathOrContent;
        std::optional<std::string> mMimeType;
    };

    std::shared_ptr<RunLoop> mRunLoop;
    uint16_t mLocalPort;

    std::shared_ptr<ServerSocket> mSocketTLS;

    // Protects mStaticFiles and mWebSocketHandlerFactories below.
    std::mutex mContentLock;

    std::unordered_map<std::string, StaticFileInfo> mStaticFiles;

    std::unordered_map<std::string, WebSocketHandlerFactory>
        mWebSocketHandlerFactories;

    void handleWebSocketRequest(
            ClientSocket *clientSocket,
            WebSocketHandlerFactory factory,
            const HTTPRequest &request,
            int32_t *httpResultCode,
            std::unordered_map<std::string, std::string> *responseHeaders,
            std::string *body);

    void handleStaticFileRequest(
            const StaticFileInfo &info,
            const HTTPRequest &request,
            int32_t *httpResultCode,
            std::unordered_map<std::string, std::string> *responseHeaders,
            std::string *body);

    static std::string GuessMimeType(const std::string &path);
};
