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

#include <https/HTTPServer.h>

#include <https/ClientSocket.h>
#include <https/HTTPRequestResponse.h>
#include <https/Support.h>
#include "common/libs/utils/base64.h"

#include <android-base/logging.h>

#include <iostream>
#include <map>
#include <string>

#include <openssl/sha.h>

#define CC_SHA1_CTX     SHA_CTX
#define CC_SHA1_Init    SHA1_Init
#define CC_SHA1_Update  SHA1_Update
#define CC_SHA1_Final   SHA1_Final
#define CC_LONG         size_t

HTTPServer::HTTPServer(
        std::shared_ptr<RunLoop> runLoop,
        const char *iface,
        uint16_t port,
        ServerSocket::TransportType transportType,
        const std::optional<std::string> &certificate_pem_path,
        const std::optional<std::string> &private_key_pem_path)
    : mRunLoop(runLoop),
      mLocalPort(port),
      mSocketTLS(
              std::make_shared<ServerSocket>(
                  this,
                  transportType,
                  iface ? iface : "0.0.0.0",
                  port,
                  certificate_pem_path,
                  private_key_pem_path)) {
    CHECK(mSocketTLS->initCheck() == 0);
}

uint16_t HTTPServer::getLocalPort() const {
    return mLocalPort;
}

void HTTPServer::run() {
    mSocketTLS->run(mRunLoop);
}

bool HTTPServer::handleSingleRequest(
        ClientSocket *clientSocket,
        const uint8_t *data,
        size_t size,
        bool isEOS) {
    (void)isEOS;

    static const std::unordered_map<int32_t, std::string> kStatusMessage {
        { 101, "Switching Protocols" },
        { 200, "OK" },
        { 400, "Bad Request" },
        { 404, "Not Found" },
        { 405, "Method Not Allowed" },
        { 503, "Service Unavailable" },
        { 505, "HTTP Version Not Supported" },
    };

    HTTPRequest request;
    request.setTo(data, size);

    int32_t httpResultCode;
    std::string body;
    std::unordered_map<std::string, std::string> responseHeaders;

    if (request.initCheck() < 0) {
        httpResultCode = 400;  // Bad Request
    } else if (request.getMethod() != "GET") {
        httpResultCode = 405;  // Method Not Allowed
    } else if (request.getVersion() != "HTTP/1.1") {
        httpResultCode = 505;  // HTTP Version Not Supported
    } else {
        httpResultCode = 404;

        auto path = request.getPath();

        std::string query;

        auto separatorPos = path.find('?');
        if (separatorPos != std::string::npos) {
            query = path.substr(separatorPos);
            path.erase(separatorPos);
        }

        if (path == "/") { path = "/index.html"; }

        bool done = false;

        {
            std::lock_guard autoLock(mContentLock);

            auto it = mStaticFiles.find(path);

            if (it != mStaticFiles.end()) {
                handleStaticFileRequest(
                        it->second,
                        request,
                        &httpResultCode,
                        &responseHeaders,
                        &body);

                done = true;
            }
        }

        if (!done) {
            std::lock_guard autoLock(mContentLock);

            auto it = mWebSocketHandlerFactories.find(path);

            if (it != mWebSocketHandlerFactories.end()) {
                handleWebSocketRequest(
                        clientSocket,
                        it->second,
                        request,
                        &httpResultCode,
                        &responseHeaders,
                        &body);

                done = true;
            }
        }

        const auto remoteAddr = clientSocket->remoteAddr();
        uint32_t ip = ntohl(remoteAddr.sin_addr.s_addr);

        LOG(INFO)
            << (ip >> 24)
            << "."
            << ((ip >> 16) & 0xff)
            << "."
            << ((ip >> 8) & 0xff)
            << "."
            << (ip & 0xff)
            << ":"
            << ntohs(remoteAddr.sin_port)
            << " "
            << httpResultCode << " \"" << path << "\"";
    }

    const std::string status =
        std::to_string(httpResultCode)
            + " "
            + kStatusMessage.find(httpResultCode)->second;

    bool closeConnection = false;

    if (httpResultCode != 200 && httpResultCode != 101) {
        body = "<h1>" + status + "</h1>";

        responseHeaders["Connection"] = "close";
        responseHeaders["Content-Type"] = "text/html";

        closeConnection = true;
    }

    std::string value;
    if (request.getHeaderField("Connection", &value) && value == "close") {
        LOG(VERBOSE) << "Closing connection per client's request.";
        closeConnection = true;
    }

    responseHeaders["Content-Length"] = std::to_string(body.size());

    if (closeConnection) {
        responseHeaders["Connection"] = "close";
    }

    std::string response;
    response = "HTTP/1.1 " + status + "\r\n";

    for (const auto &pair : responseHeaders) {
        response += pair.first + ": " + pair.second + "\r\n";
    }

    response += "\r\n";

    clientSocket->queueResponse(response, body);

    return closeConnection;
}

void HTTPServer::addStaticFile(
        const char *at, const char *path, std::optional<std::string> mimeType) {
    std::lock_guard autoLock(mContentLock);
    mStaticFiles[at] = { path, mimeType };
}

void HTTPServer::addStaticContent(
        const char *at,
        const void *_data,
        size_t size,
        std::optional<std::string> mimeType) {
    if (!mimeType) {
        // Note: unlike for static, file-based content, we guess the mime type
        // based on the path we're mapping the content at, not the path it's
        // originating from (since we don't know that for memory based content).
        mimeType = GuessMimeType(at);
    }

    auto data = static_cast<const uint8_t *>(_data);

    std::lock_guard autoLock(mContentLock);
    mStaticFiles[at] = { std::vector<uint8_t>(data, data + size), mimeType };
}

void HTTPServer::addWebSocketHandlerFactory(
        const char *at, WebSocketHandlerFactory factory) {
    std::lock_guard autoLock(mContentLock);
    mWebSocketHandlerFactories[at] = factory;
}

void HTTPServer::handleWebSocketRequest(
        ClientSocket *clientSocket,
        WebSocketHandlerFactory factory,
        const HTTPRequest &request,
        int32_t *httpResultCode,
        std::unordered_map<std::string, std::string> *responseHeaders,
        std::string *body) {
    (void)body;

    auto [status, handler] = factory();

    if (status != 0 || !handler) {
        *httpResultCode = 503;  // Service unavailable.
        return;
    }

    *httpResultCode = 400;

    std::string value;
    if (!request.getHeaderField("Connection", &value)
            || (value != "Upgrade" && value != "keep-alive, Upgrade")) {
        return;
    }

    if (!request.getHeaderField("Upgrade", &value) || value != "websocket") {
        return;
    }

    if (!request.getHeaderField("Sec-WebSocket-Version", &value)) {
        return;
    }

    char *end;
    long version = strtol(value.c_str(), &end, 10);

    if (end == value.c_str() || *end != '\0' || version < 13) {
        return;
    }

    if (!request.getHeaderField("Sec-WebSocket-Key", &value)) {
        return;
    }

    *httpResultCode = 101;

    (*responseHeaders)["Connection"] = "Upgrade";
    (*responseHeaders)["Upgrade"] = "websocket";

    std::string tmp = value;
    tmp += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    CC_SHA1_CTX ctx;
    int res = CC_SHA1_Init(&ctx);
    CHECK_EQ(res, 1);

    res = CC_SHA1_Update(
                &ctx, tmp.c_str(), static_cast<CC_LONG>(tmp.size()));

    CHECK_EQ(res, 1);

    unsigned char digest[20];  // 160 bit
    res = CC_SHA1_Final(digest, &ctx);
    CHECK_EQ(res, 1);

    std::string acceptKey;
    cvd::EncodeBase64(digest, sizeof(digest), &acceptKey);

    (*responseHeaders)["Sec-WebSocket-Accept"] = acceptKey;

    clientSocket->setWebSocketHandler(handler);
}

void HTTPServer::handleStaticFileRequest(
        const StaticFileInfo &info,
        const HTTPRequest &request,
        int32_t *httpResultCode,
        std::unordered_map<std::string, std::string> *responseHeaders,
        std::string *body) {
    (void)request;

    if (std::holds_alternative<std::string>(info.mPathOrContent)) {
        const auto &path = std::get<std::string>(info.mPathOrContent);

        std::unique_ptr<FILE, std::function<int(FILE *)>> file(
                fopen(path.c_str(), "r"),
                fclose);

        if (!file) {
            *httpResultCode = 404;
            return;
        }

        fseek(file.get(), 0, SEEK_END);
        long fileSize = ftell(file.get());
        fseek(file.get(), 0, SEEK_SET);

        (*responseHeaders)["Content-Length"] = std::to_string(fileSize);

        if (info.mMimeType) {
            (*responseHeaders)["Content-Type"] = *info.mMimeType;
        } else {
            (*responseHeaders)["Content-Type"] = GuessMimeType(path);
        }

        while (!feof(file.get())) {
            char buffer[1024];
            auto n = fread(buffer, 1, sizeof(buffer), file.get());

            body->append(buffer, n);
        }
    } else {
        CHECK(std::holds_alternative<std::vector<uint8_t>>(
                    info.mPathOrContent));

        const auto &content =
            std::get<std::vector<uint8_t>>(info.mPathOrContent);

        body->append(content.begin(), content.end());

        (*responseHeaders)["Content-Length"] = std::to_string(content.size());
    }

    *httpResultCode = 200;
}

// static
std::string HTTPServer::GuessMimeType(const std::string &path) {
    auto dotPos = path.rfind('.');
    if (dotPos != std::string::npos) {
        auto extension = std::string(path, dotPos + 1);

        static std::unordered_map<std::string, std::string>
            sMimeTypeByExtension {

            { "html", "text/html" },
            { "htm", "text/html" },
            { "css", "text/css" },
            { "js", "text/javascript" },
        };

        auto it = sMimeTypeByExtension.find(extension);
        if (it != sMimeTypeByExtension.end()) {
            return it->second;
        }
    }

    return "application/octet-stream";
}

std::optional<std::string> HTTPServer::certificate_pem_path() const {
    return mSocketTLS->certificate_pem_path();
}

std::optional<std::string> HTTPServer::private_key_pem_path() const {
    return mSocketTLS->private_key_pem_path();
}
