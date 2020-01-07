#pragma once

#include <https/RunLoop.h>
#include <source/StreamingSink.h>

#include <memory>
#include <vector>

#include <linux/input.h>
#include <linux/uinput.h>

namespace android {

struct CVMTouchSink
    : public StreamingSink, public std::enable_shared_from_this<CVMTouchSink> {
    explicit CVMTouchSink(std::shared_ptr<RunLoop> runLoop, int serverFd);
    ~CVMTouchSink() override;

    void start();

    void onAccessUnit(const sp<ABuffer> &accessUnit) override;

private:
    std::shared_ptr<RunLoop> mRunLoop;
    int mServerFd;

    int mClientFd;

    std::mutex mLock;
    std::vector<uint8_t> mOutBuffer;
    bool mSendPending;

    void onServerConnection();
    void onSocketSend();

    void sendEvents(const std::vector<input_event> &events);
};

}  // namespace android


