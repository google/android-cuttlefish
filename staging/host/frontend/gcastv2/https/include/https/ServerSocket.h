#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct ClientSocket;
struct HTTPServer;
struct RunLoop;

struct ServerSocket : public std::enable_shared_from_this<ServerSocket> {
    enum class TransportType {
        TCP,
        TLS,
    };

    explicit ServerSocket(
            HTTPServer *server,
            TransportType transportType,
            const char *iface,
            uint16_t port,
            const std::optional<std::string> &certificate_pem_path,
            const std::optional<std::string> &private_key_pem_path);

    ~ServerSocket();

    ServerSocket(const ServerSocket &) = delete;
    ServerSocket &operator=(const ServerSocket &) = delete;

    int initCheck() const;

    TransportType transportType() const;

    int run(std::shared_ptr<RunLoop> rl);

    void onClientSocketClosed(int sock);

    std::optional<std::string> certificate_pem_path() const;
    std::optional<std::string> private_key_pem_path() const;

private:
    int mInitCheck;
    HTTPServer *mServer;
    std::optional<std::string> mCertificatePath, mPrivateKeyPath;
    int mSocket;
    TransportType mTransportType;

    std::shared_ptr<RunLoop> mRunLoop;
    std::vector<std::shared_ptr<ClientSocket>> mClientSockets;

    void acceptIncomingConnection();
};

