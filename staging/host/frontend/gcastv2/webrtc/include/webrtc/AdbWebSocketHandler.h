#pragma once

#include <https/WebSocketHandler.h>
#include <https/RunLoop.h>

#include <memory>

struct AdbWebSocketHandler
    : public WebSocketHandler,
      public std::enable_shared_from_this<AdbWebSocketHandler> {

    explicit AdbWebSocketHandler(
            std::shared_ptr<RunLoop> runLoop,
            const std::string &adb_host_and_port);

    ~AdbWebSocketHandler() override;

    void run();

    int handleMessage(
            uint8_t headerByte, const uint8_t *msg, size_t len) override;

private:
    struct AdbConnection;

    std::shared_ptr<RunLoop> mRunLoop;
    std::shared_ptr<AdbConnection> mAdbConnection;

    int mSocket;

    int setupSocket(const std::string &adb_host_and_port);
};

