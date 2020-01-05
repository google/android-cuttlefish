#pragma once

#include <https/RunLoop.h>

#include <sys/socket.h>

struct BufferedSocket {
    explicit BufferedSocket(std::shared_ptr<RunLoop> rl, int sock);
    virtual ~BufferedSocket();

    BufferedSocket(const BufferedSocket &) = delete;
    BufferedSocket &operator=(const BufferedSocket &) = delete;

    virtual void postRecv(RunLoop::AsyncFunction fn) = 0;
    virtual void postSend(RunLoop::AsyncFunction fn) = 0;

    ssize_t recv(void *data, size_t size) {
        return recvfrom(data, size, nullptr, nullptr);
    }

    virtual ssize_t recvfrom(
            void *data,
            size_t size,
            sockaddr *address,
            socklen_t *addressLen) = 0;

    ssize_t send(const void *data, size_t size) {
        return sendto(data, size, nullptr, 0);
    }

    virtual ssize_t sendto(
            const void *data,
            size_t size,
            const sockaddr *addr,
            socklen_t addrLen) = 0;

    virtual void postFlush(RunLoop::AsyncFunction fn) = 0;

    int fd() const;

protected:
    RunLoop *runLoop();

private:
    std::shared_ptr<RunLoop> mRunLoop;
    int mSock;
};
