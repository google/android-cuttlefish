#pragma once

#include <https/RunLoop.h>
#include <source/StreamingSink.h>

#include <functional>
#include <memory>
#include <vector>

namespace android {

struct TouchSink
    : public StreamingSink, public std::enable_shared_from_this<TouchSink> {
    explicit TouchSink(std::shared_ptr<RunLoop> runLoop, int serverFd,
                       bool write_virtio_input);
    ~TouchSink() override;

    void start();

    void onAccessUnit(const std::shared_ptr<ABuffer> &accessUnit) override;

private:
    std::shared_ptr<RunLoop> mRunLoop;
    int mServerFd;

    int mClientFd;

    std::mutex mLock;
    std::vector<uint8_t> mOutBuffer;
    bool mSendPending;

    std::function<void(int32_t, int32_t, bool)> send_event_;
    std::function<void(int32_t, int32_t, int32_t, bool, int32_t)> send_mt_event_;

    void onServerConnection();
    void onSocketSend();

    void sendRawEvents(const void* evt_buffer, size_t length);
};

}  // namespace android


