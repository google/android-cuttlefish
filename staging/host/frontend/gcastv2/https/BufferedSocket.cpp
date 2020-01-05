#include <https/BufferedSocket.h>

#include <cassert>
#include <unistd.h>

BufferedSocket::BufferedSocket(std::shared_ptr<RunLoop> rl, int sock)
    : mRunLoop(rl),
      mSock(sock) {
    assert(mSock >= 0);
}

BufferedSocket::~BufferedSocket() {
    mRunLoop->cancelSocket(mSock);

    close(mSock);
    mSock = -1;
}

int BufferedSocket::fd() const {
    return mSock;
}

RunLoop *BufferedSocket::runLoop() {
    return mRunLoop.get();
}
