#include <webrtc/AdbWebSocketHandler.h>
#include <webrtc/DTLS.h>
#include <webrtc/MyWebSocketHandler.h>
#include <webrtc/RTPSocketHandler.h>
#include <webrtc/ServerState.h>
#include <webrtc/STUNMessage.h>

#include <https/HTTPServer.h>
#include <https/PlainSocket.h>
#include <https/RunLoop.h>
#include <https/SafeCallbackable.h>
#include <https/SSLSocket.h>
#include <https/Support.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/Utils.h>

#include <iostream>
#include <unordered_map>

#if defined(TARGET_ANDROID)
#include <gflags/gflags.h>

DEFINE_string(
        public_ip,
        "0.0.0.0",
        "Public IPv4 address of your server, a.b.c.d format");
DEFINE_string(
        assets_dir,
        "webrtc",
        "Directory with location of webpage assets.");
DEFINE_string(
        certs_dir,
        "webrtc/certs",
        "Directory to certificates.");

DEFINE_int32(touch_fd, -1, "An fd to listen on for touch connections.");
DEFINE_int32(keyboard_fd, -1, "An fd to listen on for keyboard connections.");

DEFINE_string(adb, "", "Interface:port of local adb service.");
#endif

int main(int argc, char **argv) {
#if defined(TARGET_ANDROID)
    ::gflags::ParseCommandLineFlags(&argc, &argv, true);
#else
    (void)argc;
    (void)argv;
#endif

    SSLSocket::Init();
    DTLS::Init();

    auto runLoop = RunLoop::main();

    auto state = std::make_shared<ServerState>(
            runLoop, ServerState::VideoFormat::VP8);

#if 1
    auto port = 8443;  // Change to 8080 to use plain http instead of https.

    auto httpd = std::make_shared<HTTPServer>(
            runLoop,
            "0.0.0.0",
            port,
            port == 8080
                ? ServerSocket::TransportType::TCP
                : ServerSocket::TransportType::TLS,
            FLAGS_certs_dir + "/server.crt",
            FLAGS_certs_dir + "/server.key");

    const std::string index_html = FLAGS_assets_dir + "/index.html";
    const std::string receive_js = FLAGS_assets_dir + "/js/receive.js";
    const std::string logcat_js = FLAGS_assets_dir + "/js/logcat.js";
    const std::string style_css = FLAGS_assets_dir + "/style.css";

    httpd->addStaticFile("/index.html", index_html.c_str());
    httpd->addStaticFile("/js/receive.js", receive_js.c_str());
    httpd->addStaticFile("/js/logcat.js", logcat_js.c_str());
    httpd->addStaticFile("/style.css", style_css.c_str());

    httpd->addWebSocketHandlerFactory(
            "/control",
            [runLoop, state]{
                auto id = state->acquireHandlerId();

                auto handler =
                    std::make_shared<MyWebSocketHandler>(runLoop, state, id);

                return std::make_pair(0 /* OK */, handler);
            });

#if defined(TARGET_ANDROID)
    if (!FLAGS_adb.empty()) {
        httpd->addWebSocketHandlerFactory(
                "/control_adb",
                [runLoop]{
                    auto handler = std::make_shared<AdbWebSocketHandler>(
                            runLoop, FLAGS_adb);

                    handler->run();

                    return std::make_pair(0 /* OK */, handler);
                });
    }
#endif

    httpd->run();
#else
    uint16_t receiverPort = 63843;
    std::string receiverUFrag = "N1NB";
    std::string receiverPassword = "deadbeef";

    uint16_t senderPort = 63844;
    std::string senderUFrag = "ABCD";
    std::string senderPassword = "wooops";

    auto sender = std::make_shared<RTPSocketHandler>(
            runLoop,
            RTPSocketHandler::Mode::CONTROLLER,
            AF_INET,
            senderPort,
            false /* isChrome */);

    sender->addSession(
            senderUFrag,
            senderPassword,
            receiverUFrag,
            receiverPassword);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(senderPort);

    sockaddr_storage senderAddr;
    memcpy(&senderAddr, &addr, sizeof(addr));

    auto receiver = std::make_shared<RTPSocketHandler>(
            runLoop,
            RTPSocketHandler::Mode::CONTROLLEE,
            AF_INET,
            receiverPort,
            false /* isChrome */);

    receiver->addSession(
            receiverUFrag,
            receiverPassword,
            senderUFrag,
            senderPassword,
            senderAddr);

    sender->run();
    receiver->run();

    receiver->kick();
#endif

    runLoop->run();

    return 0;
}
