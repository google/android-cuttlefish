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
