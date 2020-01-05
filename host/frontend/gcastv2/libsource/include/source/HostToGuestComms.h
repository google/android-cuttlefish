#pragma once

#include <https/RunLoop.h>

#include <sys/socket.h>
#include "common/libs/fs/vm_sockets.h"

#include <memory>

struct HostToGuestComms : std::enable_shared_from_this<HostToGuestComms> {
    using ReceiveCb = std::function<void(const void *data, size_t size)>;

    // Used to communicate with the guest userspace "RemoterService".
    static constexpr uint16_t kPortMain = 8555;

    // Used to carry updated framebuffers from guest to host.
    static constexpr uint16_t kPortVideo = 5580;

    // Used to carry audio data from guest to host.
    static constexpr uint16_t kPortAudio = 8556;

    explicit HostToGuestComms(
            std::shared_ptr<RunLoop> runLoop,
            bool isServer,
            uint32_t cid,
            uint16_t port,
            ReceiveCb onReceive);

    explicit HostToGuestComms(
            std::shared_ptr<RunLoop> runLoop,
            bool isServer,
            int fd,
            ReceiveCb onReceive);

    ~HostToGuestComms();

    void start();

    void send(const void *data, size_t size, bool addFraming = true);

private:
    std::shared_ptr<RunLoop> mRunLoop;
    bool mIsServer;
    ReceiveCb mOnReceive;

    int mServerSock;
    int mSock;
    sockaddr_vm mConnectToAddr;

    std::vector<uint8_t> mInBuffer;
    size_t mInBufferLen;

    std::mutex mLock;
    std::vector<uint8_t> mOutBuffer;
    bool mSendPending;

    bool mConnected;

    void onServerConnection();
    void onSocketReceive();
    void onSocketSend();
    void drainInBuffer();

    void onAttemptToConnect(const sockaddr_vm &addr);
    void onCheckConnection(const sockaddr_vm &addr);
    void onConnected();
};

