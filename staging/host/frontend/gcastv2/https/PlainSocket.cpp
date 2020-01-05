#include <https/PlainSocket.h>

#include <sys/socket.h>

PlainSocket::PlainSocket(std::shared_ptr<RunLoop> rl, int sock)
    : BufferedSocket(rl, sock) {
}

void PlainSocket::postRecv(RunLoop::AsyncFunction fn) {
    runLoop()->postSocketRecv(fd(), fn);
}

void PlainSocket::postSend(RunLoop::AsyncFunction fn) {
    runLoop()->postSocketSend(fd(), fn);
}

ssize_t PlainSocket::recvfrom(
        void *data,
        size_t size,
        sockaddr *address,
        socklen_t *addressLen) {
    return ::recvfrom(fd(), data, size, 0, address, addressLen);
}

ssize_t PlainSocket::sendto(
        const void *data,
        size_t size,
        const sockaddr *addr,
        socklen_t addrLen) {
    if (!addr) {
        return ::send(fd(), data, size, 0);
    }
    return ::sendto(fd(), data, size, 0, addr, addrLen);
}

void PlainSocket::postFlush(RunLoop::AsyncFunction fn) {
    fn();
}
