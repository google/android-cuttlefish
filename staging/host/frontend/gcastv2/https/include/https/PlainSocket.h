#pragma once

#include <https/BufferedSocket.h>

struct PlainSocket : public BufferedSocket {
    explicit PlainSocket(std::shared_ptr<RunLoop> rl, int sock);

    PlainSocket(const PlainSocket &) = delete;
    PlainSocket &operator=(const PlainSocket &) = delete;

    void postRecv(RunLoop::AsyncFunction fn) override;
    void postSend(RunLoop::AsyncFunction fn) override;

    ssize_t recvfrom(
            void *data,
            size_t size,
            sockaddr *address,
            socklen_t *addressLen) override;

    ssize_t sendto(
            const void *data,
            size_t size,
            const sockaddr *addr,
            socklen_t addrLen) override;

    void postFlush(RunLoop::AsyncFunction fn) override;
};
