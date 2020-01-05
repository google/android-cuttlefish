#include <https/BufferedSocket.h>

#include <https/WebSocketHandler.h>

#include <arpa/inet.h>
#include <vector>
#include <memory>

#include <https/PlainSocket.h>
#include <https/SSLSocket.h>

struct HTTPServer;
struct RunLoop;
struct ServerSocket;

struct ClientSocket : public std::enable_shared_from_this<ClientSocket> {
    explicit ClientSocket(
            std::shared_ptr<RunLoop> rl,
            HTTPServer *server,
            ServerSocket *parent,
            const sockaddr_in &addr,
            int sock);

    ClientSocket(const ClientSocket &) = delete;
    ClientSocket &operator=(const ClientSocket &) = delete;

    void run();

    int fd() const;

    void queueResponse(const std::string &response, const std::string &body);
    void setWebSocketHandler(std::shared_ptr<WebSocketHandler> handler);

    void queueOutputData(const uint8_t *data, size_t size);

    sockaddr_in remoteAddr() const;

private:
    std::shared_ptr<RunLoop> mRunLoop;
    HTTPServer *mServer;
    ServerSocket *mParent;
    sockaddr_in mRemoteAddr;

    std::shared_ptr<BufferedSocket> mImplPlain;
    std::shared_ptr<SSLSocket> mImplSSL;

    std::vector<uint8_t> mInBuffer;
    size_t mInBufferLen;

    std::vector<uint8_t> mOutBuffer;
    bool mSendPending;

    bool mDisconnecting;

    std::shared_ptr<WebSocketHandler> mWebSocketHandler;

    void handleIncomingData();

    // Returns true iff the client should close the connection.
    bool handleRequest(bool isEOS);

    void sendOutputData();

    void disconnect();
    void finishDisconnect();

    BufferedSocket *getImpl() const {
        return mImplSSL ? mImplSSL.get() : mImplPlain.get();
    }
};
