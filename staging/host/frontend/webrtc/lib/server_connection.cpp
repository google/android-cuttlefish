//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
//

#include "host/frontend/webrtc/lib/server_connection.h"

#include <android-base/logging.h>
#include <libwebsockets.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/files.h"

namespace cuttlefish {
namespace webrtc_streaming {

// ServerConnection over Unix socket
class UnixServerConnection : public ServerConnection {
 public:
  UnixServerConnection(const std::string& addr,
                       std::weak_ptr<ServerConnectionObserver> observer);
  ~UnixServerConnection() override;

  bool Send(const Json::Value& msg) override;

 private:
  void Connect() override;
  void StopThread();
  void ReadLoop();

  const std::string addr_;
  SharedFD conn_;
  std::mutex write_mtx_;
  std::weak_ptr<ServerConnectionObserver> observer_;
  // The event fd must be declared before the thread to ensure it's initialized
  // before the thread starts and is safe to be accessed from it.
  SharedFD thread_notifier_;
  std::atomic_bool running_ = false;
  std::thread thread_;
};

// ServerConnection using websockets
class WsConnectionContext;

class WsConnection : public std::enable_shared_from_this<WsConnection> {
 public:
  struct CreateConnectionSul {
    lws_sorted_usec_list_t sul = {};
    std::weak_ptr<WsConnection> weak_this;
  };

  WsConnection(int port, const std::string& addr, const std::string& path,
               ServerConfig::Security secure,
               const std::vector<std::pair<std::string, std::string>>& headers,
               std::weak_ptr<ServerConnectionObserver> observer,
               std::shared_ptr<WsConnectionContext> context);

  ~WsConnection();

  void Connect();
  bool Send(const Json::Value& msg);

  void ConnectInner();

  void OnError(const std::string& error);
  void OnReceive(const uint8_t* data, size_t len, bool is_binary);
  void OnOpen();
  void OnClose();
  void OnWriteable();

  void AddHttpHeaders(unsigned char** p, unsigned char* end) const;

 private:
  struct WsBuffer {
    WsBuffer() = default;
    WsBuffer(const uint8_t* data, size_t len, bool binary)
        : buffer_(LWS_PRE + len), is_binary_(binary) {
      memcpy(&buffer_[LWS_PRE], data, len);
    }

    uint8_t* data() { return &buffer_[LWS_PRE]; }
    bool is_binary() const { return is_binary_; }
    size_t size() const { return buffer_.size() - LWS_PRE; }

   private:
    std::vector<uint8_t> buffer_;
    bool is_binary_;
  };
  bool Send(const uint8_t* data, size_t len, bool binary = false);

  CreateConnectionSul extended_sul_;
  struct lws* wsi_;
  const int port_;
  const std::string addr_;
  const std::string path_;
  const ServerConfig::Security security_;
  const std::vector<std::pair<std::string, std::string>> headers_;

  std::weak_ptr<ServerConnectionObserver> observer_;

  // each element contains the data to be sent and whether it's binary or not
  std::deque<WsBuffer> write_queue_;
  std::mutex write_queue_mutex_;
  // The connection object should not outlive the context object. This reference
  // guarantees it.
  std::shared_ptr<WsConnectionContext> context_;
};

class WsConnectionContext
    : public std::enable_shared_from_this<WsConnectionContext> {
 public:
  static std::shared_ptr<WsConnectionContext> Create();

  WsConnectionContext(struct lws_context* lws_ctx);
  ~WsConnectionContext();

  std::unique_ptr<ServerConnection> CreateConnection(
      int port, const std::string& addr, const std::string& path,
      ServerConfig::Security secure,
      std::weak_ptr<ServerConnectionObserver> observer,
      const std::vector<std::pair<std::string, std::string>>& headers);

  void RememberConnection(void*, std::weak_ptr<WsConnection>);
  void ForgetConnection(void*);
  std::shared_ptr<WsConnection> GetConnection(void*);

  struct lws_context* lws_context() {
    return lws_context_;
  }

 private:
  void Start();

  std::map<void*, std::weak_ptr<WsConnection>> weak_by_ptr_;
  std::mutex map_mutex_;
  struct lws_context* lws_context_;
  std::thread message_loop_;
};

std::unique_ptr<ServerConnection> ServerConnection::Connect(
    const ServerConfig& conf,
    std::weak_ptr<ServerConnectionObserver> observer) {
  std::unique_ptr<ServerConnection> ret;
  // If the provided address points to an existing UNIX socket in the file
  // system connect to it, otherwise assume it's a network address and connect
  // using websockets
  if (FileIsSocket(conf.addr)) {
    ret.reset(new UnixServerConnection(conf.addr, observer));
  } else {
    // This can be a local variable since the ws connection will keep a
    // reference to it.
    auto ws_context = WsConnectionContext::Create();
    CHECK(ws_context) << "Failed to create websocket context";
    ret = ws_context->CreateConnection(conf.port, conf.addr, conf.path,
                                       conf.security, observer,
                                       conf.http_headers);
  }
  ret->Connect();
  return ret;
}

void ServerConnection::Reconnect() { Connect(); }

// UnixServerConnection implementation

UnixServerConnection::UnixServerConnection(
    const std::string& addr, std::weak_ptr<ServerConnectionObserver> observer)
    : addr_(addr), observer_(observer) {}

UnixServerConnection::~UnixServerConnection() {
  StopThread();
}

bool UnixServerConnection::Send(const Json::Value& msg) {
  Json::StreamWriterBuilder factory;
  auto str = Json::writeString(factory, msg);
  std::lock_guard<std::mutex> lock(write_mtx_);
  auto res =
      conn_->Send(reinterpret_cast<const uint8_t*>(str.c_str()), str.size(), 0);
  if (res < 0) {
    LOG(ERROR) << "Failed to send data to signaling server: "
               << conn_->StrError();
    // Don't call OnError() here, the receiving thread probably did it already
    // or is about to do it.
  }
  // A SOCK_SEQPACKET unix socket will send the entire message or fail, but it
  // won't send a partial message.
  return res == str.size();
}

void UnixServerConnection::Connect() {
  // The thread could be running if this is a Reconnect
  StopThread();

  conn_ = SharedFD::SocketLocalClient(addr_, false, SOCK_SEQPACKET);
  if (!conn_->IsOpen()) {
    LOG(ERROR) << "Failed to connect to unix socket: " << conn_->StrError();
    if (auto o = observer_.lock(); o) {
      o->OnError("Failed to connect to unix socket");
    }
    return;
  }
  thread_notifier_ = SharedFD::Event();
  if (!thread_notifier_->IsOpen()) {
    LOG(ERROR) << "Failed to create eventfd for background thread: "
               << thread_notifier_->StrError();
    if (auto o = observer_.lock(); o) {
      o->OnError("Failed to create eventfd for background thread");
    }
    return;
  }
  if (auto o = observer_.lock(); o) {
    o->OnOpen();
  }
  // Start the thread
  running_ = true;
  thread_ = std::thread([this](){ReadLoop();});
}

void UnixServerConnection::StopThread() {
  running_ = false;
  if (!thread_notifier_->IsOpen()) {
    // The thread won't be running if this isn't open
    return;
  }
  if (thread_notifier_->EventfdWrite(1) < 0) {
    LOG(ERROR) << "Failed to notify background thread, this thread may block";
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

void UnixServerConnection::ReadLoop() {
  if (!thread_notifier_->IsOpen()) {
    LOG(ERROR) << "The UnixServerConnection's background thread is unable to "
                  "receive notifications so it can't run";
    return;
  }
  std::vector<uint8_t> buffer(4096, 0);
  while (running_) {
    SharedFDSet rset;
    rset.Set(thread_notifier_);
    rset.Set(conn_);
    auto res = Select(&rset, nullptr, nullptr, nullptr);
    if (res < 0) {
      LOG(ERROR) << "Failed to select from background thread";
      break;
    }
    if (rset.IsSet(thread_notifier_)) {
      eventfd_t val;
      auto res = thread_notifier_->EventfdRead(&val);
      if (res < 0) {
        LOG(ERROR) << "Error reading from event fd: "
                   << thread_notifier_->StrError();
        break;
      }
    }
    if (rset.IsSet(conn_)) {
      auto size = conn_->Recv(buffer.data(), 0, MSG_TRUNC | MSG_PEEK);
      if (size > buffer.size()) {
        // Enlarge enough to accommodate size bytes and be a multiple of 4096
        auto new_size = (size + 4095) & ~4095;
        buffer.resize(new_size);
      }
      auto res = conn_->Recv(buffer.data(), buffer.size(), MSG_TRUNC);
      if (res < 0) {
        LOG(ERROR) << "Failed to read from server: " << conn_->StrError();
        if (auto observer = observer_.lock(); observer) {
          observer->OnError(conn_->StrError());
        }
        return;
      }
      if (res == 0) {
        auto observer = observer_.lock();
        if (observer) {
          observer->OnClose();
        }
        break;
      }
      auto observer = observer_.lock();
      if (observer) {
        observer->OnReceive(buffer.data(), res, false);
      }
    }
  }
}

// WsConnection implementation

int LwsCallback(struct lws* wsi, enum lws_callback_reasons reason, void* user,
                void* in, size_t len);
void CreateConnectionCallback(lws_sorted_usec_list_t* sul);

namespace {

constexpr char kProtocolName[] = "cf-webrtc-device";
constexpr int kBufferSize = 65536;

const uint32_t backoff_ms[] = {1000, 2000, 3000, 4000, 5000};

const lws_retry_bo_t kRetry = {
    .retry_ms_table = backoff_ms,
    .retry_ms_table_count = LWS_ARRAY_SIZE(backoff_ms),
    .conceal_count = LWS_ARRAY_SIZE(backoff_ms),

    .secs_since_valid_ping = 3,    /* force PINGs after secs idle */
    .secs_since_valid_hangup = 10, /* hangup after secs idle */

    .jitter_percent = 20,
};

const struct lws_protocols kProtocols[2] = {
    {kProtocolName, LwsCallback, 0, kBufferSize, 0, NULL, 0},
    {NULL, NULL, 0, 0, 0, NULL, 0}};

}  // namespace

std::shared_ptr<WsConnectionContext> WsConnectionContext::Create() {
  struct lws_context_creation_info context_info = {};
  context_info.port = CONTEXT_PORT_NO_LISTEN;
  context_info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  context_info.protocols = kProtocols;
  struct lws_context* lws_ctx = lws_create_context(&context_info);
  if (!lws_ctx) {
    return nullptr;
  }
  return std::shared_ptr<WsConnectionContext>(new WsConnectionContext(lws_ctx));
}

WsConnectionContext::WsConnectionContext(struct lws_context* lws_ctx)
    : lws_context_(lws_ctx) {
  Start();
}

WsConnectionContext::~WsConnectionContext() {
  lws_context_destroy(lws_context_);
  if (message_loop_.joinable()) {
    message_loop_.join();
  }
}

void WsConnectionContext::Start() {
  message_loop_ = std::thread([this]() {
    for (;;) {
      if (lws_service(lws_context_, 0) < 0) {
        break;
      }
    }
  });
}

// This wrapper is needed because the ServerConnection objects are meant to be
// referenced by std::unique_ptr but WsConnection needs to be referenced by
// std::shared_ptr because it's also (weakly) referenced by the websocket
// thread.
class WsConnectionWrapper : public ServerConnection {
 public:
  WsConnectionWrapper(std::shared_ptr<WsConnection> conn) : conn_(conn) {}

  bool Send(const Json::Value& msg) override { return conn_->Send(msg); }

 private:
  void Connect() override { return conn_->Connect(); }
  std::shared_ptr<WsConnection> conn_;
};

std::unique_ptr<ServerConnection> WsConnectionContext::CreateConnection(
    int port, const std::string& addr, const std::string& path,
    ServerConfig::Security security,
    std::weak_ptr<ServerConnectionObserver> observer,
    const std::vector<std::pair<std::string, std::string>>& headers) {
  return std::unique_ptr<ServerConnection>(
      new WsConnectionWrapper(std::make_shared<WsConnection>(
          port, addr, path, security, headers, observer, shared_from_this())));
}

std::shared_ptr<WsConnection> WsConnectionContext::GetConnection(void* raw) {
  std::shared_ptr<WsConnection> connection;
  {
    std::lock_guard<std::mutex> lock(map_mutex_);
    if (weak_by_ptr_.count(raw) == 0) {
      return nullptr;
    }
    connection = weak_by_ptr_[raw].lock();
    if (!connection) {
      weak_by_ptr_.erase(raw);
    }
  }
  return connection;
}

void WsConnectionContext::RememberConnection(void* raw,
                                             std::weak_ptr<WsConnection> conn) {
  std::lock_guard<std::mutex> lock(map_mutex_);
  weak_by_ptr_.emplace(
      std::pair<void*, std::weak_ptr<WsConnection>>(raw, conn));
}

void WsConnectionContext::ForgetConnection(void* raw) {
  std::lock_guard<std::mutex> lock(map_mutex_);
  weak_by_ptr_.erase(raw);
}

WsConnection::WsConnection(
    int port, const std::string& addr, const std::string& path,
    ServerConfig::Security security,
    const std::vector<std::pair<std::string, std::string>>& headers,
    std::weak_ptr<ServerConnectionObserver> observer,
    std::shared_ptr<WsConnectionContext> context)
    : port_(port),
      addr_(addr),
      path_(path),
      security_(security),
      headers_(headers),
      observer_(observer),
      context_(context) {}

WsConnection::~WsConnection() {
  context_->ForgetConnection(this);
  // This will cause the callback to be called which will drop the connection
  // after seeing the context doesn't remember this object
  lws_callback_on_writable(wsi_);
}

void WsConnection::Connect() {
  memset(&extended_sul_.sul, 0, sizeof(extended_sul_.sul));
  extended_sul_.weak_this = weak_from_this();
  lws_sul_schedule(context_->lws_context(), 0, &extended_sul_.sul,
                   CreateConnectionCallback, 1);
}

void WsConnection::AddHttpHeaders(unsigned char** p, unsigned char* end) const {
  for (const auto& header_entry : headers_) {
    const auto& name = header_entry.first;
    const auto& value = header_entry.second;
    auto res = lws_add_http_header_by_name(
        wsi_, reinterpret_cast<const unsigned char*>(name.c_str()),
        reinterpret_cast<const unsigned char*>(value.c_str()), value.size(), p,
        end);
    if (res != 0) {
      LOG(ERROR) << "Unable to add header: " << name;
    }
  }
  if (!headers_.empty()) {
    // Let LWS know we added some headers.
    lws_client_http_body_pending(wsi_, 1);
  }
}

void WsConnection::OnError(const std::string& error) {
  auto observer = observer_.lock();
  if (observer) {
    observer->OnError(error);
  }
}
void WsConnection::OnReceive(const uint8_t* data, size_t len, bool is_binary) {
  auto observer = observer_.lock();
  if (observer) {
    observer->OnReceive(data, len, is_binary);
  }
}
void WsConnection::OnOpen() {
  auto observer = observer_.lock();
  if (observer) {
    observer->OnOpen();
  }
}
void WsConnection::OnClose() {
  auto observer = observer_.lock();
  if (observer) {
    observer->OnClose();
  }
}

void WsConnection::OnWriteable() {
  WsBuffer buffer;
  {
    std::lock_guard<std::mutex> lock(write_queue_mutex_);
    if (write_queue_.size() == 0) {
      return;
    }
    buffer = std::move(write_queue_.front());
    write_queue_.pop_front();
  }
  auto flags = lws_write_ws_flags(
      buffer.is_binary() ? LWS_WRITE_BINARY : LWS_WRITE_TEXT, true, true);
  auto res = lws_write(wsi_, buffer.data(), buffer.size(),
                       (enum lws_write_protocol)flags);
  if (res != buffer.size()) {
    LOG(WARNING) << "Unable to send the entire message!";
  }
}

bool WsConnection::Send(const Json::Value& msg) {
  Json::StreamWriterBuilder factory;
  auto str = Json::writeString(factory, msg);
  return Send(reinterpret_cast<const uint8_t*>(str.c_str()), str.size());
}

bool WsConnection::Send(const uint8_t* data, size_t len, bool binary) {
  if (!wsi_) {
    LOG(WARNING) << "Send called on an uninitialized connection!!";
    return false;
  }
  WsBuffer buffer(data, len, binary);
  {
    std::lock_guard<std::mutex> lock(write_queue_mutex_);
    write_queue_.emplace_back(std::move(buffer));
  }

  lws_callback_on_writable(wsi_);
  return true;
}

int LwsCallback(struct lws* wsi, enum lws_callback_reasons reason, void* user,
                void* in, size_t len) {
  constexpr int DROP = -1;
  constexpr int OK = 0;

  // For some values of `reason`, `user` doesn't point to the value provided
  // when the connection was created. This function object should be used with
  // care.
  auto with_connection =
      [wsi, user](std::function<void(std::shared_ptr<WsConnection>)> cb) {
        auto context = reinterpret_cast<WsConnectionContext*>(user);
        auto connection = context->GetConnection(wsi);
        if (!connection) {
          return DROP;
        }
        cb(connection);
        return OK;
      };

  switch (reason) {
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      return with_connection([in](std::shared_ptr<WsConnection> connection) {
        connection->OnError(in ? (char*)in : "(null)");
      });

    case LWS_CALLBACK_CLIENT_RECEIVE:
      return with_connection(
          [in, len, wsi](std::shared_ptr<WsConnection> connection) {
            connection->OnReceive((const uint8_t*)in, len,
                                  lws_frame_is_binary(wsi));
          });

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      return with_connection([](std::shared_ptr<WsConnection> connection) {
        connection->OnOpen();
      });

    case LWS_CALLBACK_CLIENT_CLOSED:
      return with_connection([](std::shared_ptr<WsConnection> connection) {
        connection->OnClose();
      });

    case LWS_CALLBACK_CLIENT_WRITEABLE:
      return with_connection([](std::shared_ptr<WsConnection> connection) {
        connection->OnWriteable();
      });

    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
      return with_connection(
          [in, len](std::shared_ptr<WsConnection> connection) {
            auto p = reinterpret_cast<unsigned char**>(in);
            auto end = (*p) + len;
            connection->AddHttpHeaders(p, end);
          });

    case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE:
      // This callback is only called when we add additional HTTP headers, let
      // LWS know we're done modifying the HTTP request.
      lws_client_http_body_pending(wsi, 0);
      return 0;

    default:
      LOG(VERBOSE) << "Unhandled value: " << reason;
      return lws_callback_http_dummy(wsi, reason, user, in, len);
  }
}

void CreateConnectionCallback(lws_sorted_usec_list_t* sul) {
  std::shared_ptr<WsConnection> connection =
      reinterpret_cast<WsConnection::CreateConnectionSul*>(sul)
          ->weak_this.lock();
  if (!connection) {
    LOG(WARNING) << "The object was already destroyed by the time of the first "
                 << "connection attempt. That's unusual.";
    return;
  }
  connection->ConnectInner();
}

void WsConnection::ConnectInner() {
  struct lws_client_connect_info connect_info;

  memset(&connect_info, 0, sizeof(connect_info));

  connect_info.context = context_->lws_context();
  connect_info.port = port_;
  connect_info.address = addr_.c_str();
  connect_info.path = path_.c_str();
  connect_info.host = connect_info.address;
  connect_info.origin = connect_info.address;
  switch (security_) {
    case ServerConfig::Security::kAllowSelfSigned:
      connect_info.ssl_connection = LCCSCF_ALLOW_SELFSIGNED |
                                    LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK |
                                    LCCSCF_USE_SSL;
      break;
    case ServerConfig::Security::kStrict:
      connect_info.ssl_connection = LCCSCF_USE_SSL;
      break;
    case ServerConfig::Security::kInsecure:
      connect_info.ssl_connection = 0;
      break;
  }
  connect_info.protocol = "webrtc-operator";
  connect_info.local_protocol_name = kProtocolName;
  connect_info.pwsi = &wsi_;
  connect_info.retry_and_idle_policy = &kRetry;
  // There is no guarantee the connection object still exists when the callback
  // is called. Put the context instead as the user data which is guaranteed to
  // still exist and holds a weak ptr to the connection.
  connect_info.userdata = context_.get();

  if (lws_client_connect_via_info(&connect_info)) {
    // wsi_ is not initialized until after the call to
    // lws_client_connect_via_info(). Luckily, this is guaranteed to run before
    // the protocol callback is called because it runs in the same loop.
    context_->RememberConnection(wsi_, weak_from_this());
  } else {
    LOG(ERROR) << "Connection failed!";
  }
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
