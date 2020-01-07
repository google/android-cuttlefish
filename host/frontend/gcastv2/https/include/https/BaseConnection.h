#pragma once

#include <https/BufferedSocket.h>
#include <https/RunLoop.h>

#include <memory>
#include <vector>

struct BaseConnection : public std::enable_shared_from_this<BaseConnection> {
    explicit BaseConnection(std::shared_ptr<RunLoop> runLoop, int sock);
    virtual ~BaseConnection() = default;

    void run();

    BaseConnection(const BaseConnection &) = delete;
    BaseConnection &operator=(const BaseConnection &) = delete;

protected:
    // Return -EAGAIN to indicate that not enough data was provided (yet).
    // Return a positive (> 0) value to drain some amount of data.
    // Return values <= 0 are considered an error.
    virtual ssize_t processClientRequest(const void *data, size_t size) = 0;

    virtual void onDisconnect(int err) = 0;

    void send(const void *_data, size_t size);

    int fd() const;

private:
    std::shared_ptr<RunLoop> mRunLoop;

    std::unique_ptr<BufferedSocket> mSocket;

    std::vector<uint8_t> mInBuffer;
    size_t mInBufferLen;

    bool mSendPending;
    std::vector<uint8_t> mOutBuffer;

    void receiveClientRequest();
    void sendOutputData();

    void onClientRequest();
};
