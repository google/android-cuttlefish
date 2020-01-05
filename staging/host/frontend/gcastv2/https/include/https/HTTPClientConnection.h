#pragma once

#include <arpa/inet.h>
#include <https/BufferedSocket.h>
#include <https/ServerSocket.h>
#include <https/WebSocketHandler.h>

#include <memory>
#include <optional>
#include <string>

struct HTTPClientConnection
    : public std::enable_shared_from_this<HTTPClientConnection> {

    explicit HTTPClientConnection(
            std::shared_ptr<RunLoop> rl,
            std::shared_ptr<WebSocketHandler> webSocketHandler,
            std::string_view path,
            ServerSocket::TransportType transportType,
            const std::optional<std::string> &trusted_pem_path =  std::nullopt);

    HTTPClientConnection(const HTTPClientConnection &) = delete;
    HTTPClientConnection &operator=(const HTTPClientConnection &) = delete;

    int initCheck() const;

    int connect(const char *host, uint16_t port);

private:
    int mInitCheck;

    std::shared_ptr<RunLoop> mRunLoop;
    std::shared_ptr<WebSocketHandler> mWebSocketHandler;
    std::string mPath;
    ServerSocket::TransportType mTransportType;
    std::shared_ptr<BufferedSocket> mImpl;

    std::vector<uint8_t> mOutBuffer;
    bool mSendPending;

    std::vector<uint8_t> mInBuffer;
    size_t mInBufferLen;

    sockaddr_in mRemoteAddr;

    bool mWebSocketMode;

    void sendRequest();
    void receiveResponse();

    // Returns true iff response was received fully.
    bool handleResponse(bool isEOS);

    void queueOutputData(const uint8_t *data, size_t size);
    void sendOutputData();
};

