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

#include "webrtc/ws_connection.h"

#include "android-base/logging.h"
#include "libwebsockets.h"

class WsConnectionContextImpl;

class WsConnectionImpl : public WsConnection,
                         public std::enable_shared_from_this<WsConnectionImpl> {
 public:
  struct CreateConnectionSul {
    lws_sorted_usec_list_t sul = {};
    std::weak_ptr<WsConnectionImpl> weak_this;
  };

  WsConnectionImpl(int port, const std::string& addr, const std::string& path,
                   Security secure,
                   std::weak_ptr<WsConnectionObserver> observer,
                   std::shared_ptr<WsConnectionContextImpl> context);

  ~WsConnectionImpl() override;

  void Connect() override;
  void ConnectInner();

  bool Send(const uint8_t* data, size_t len, bool binary = false) override;

  void OnError(const std::string& error);
  void OnReceive(const uint8_t* data, size_t len, bool is_binary);
  void OnOpen();
  void OnClose();
  void OnWriteable();

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

  CreateConnectionSul extended_sul_;
  struct lws* wsi_;
  const int port_;
  const std::string addr_;
  const std::string path_;
  const Security security_;

  std::weak_ptr<WsConnectionObserver> observer_;

  // each element contains the data to be sent and whether it's binary or not
  std::deque<WsBuffer> write_queue_;
  std::mutex write_queue_mutex_;
  // The connection object should not outlive the context object. This reference
  // guarantees it.
  std::shared_ptr<WsConnectionContextImpl> context_;
};

class WsConnectionContextImpl
    : public WsConnectionContext,
      public std::enable_shared_from_this<WsConnectionContextImpl> {
 public:
  WsConnectionContextImpl(struct lws_context* lws_ctx);
  ~WsConnectionContextImpl() override;

  std::shared_ptr<WsConnection> CreateConnection(
      int port, const std::string& addr, const std::string& path,
      WsConnection::Security secure,
      std::weak_ptr<WsConnectionObserver> observer) override;

  void RememberConnection(void*, std::weak_ptr<WsConnectionImpl>);
  void ForgetConnection(void*);
  std::shared_ptr<WsConnectionImpl> GetConnection(void*);

  struct lws_context* lws_context() {
    return lws_context_;
  }

 private:
  void Start();

  std::map<void*, std::weak_ptr<WsConnectionImpl>> weak_by_ptr_;
  std::mutex map_mutex_;
  struct lws_context* lws_context_;
  std::thread message_loop_;
};

int LwsCallback(struct lws* wsi, enum lws_callback_reasons reason, void* user,
                void* in, size_t len);
void CreateConnectionCallback(lws_sorted_usec_list_t* sul);

namespace {

constexpr char kProtocolName[] = "lws-websocket-protocol";
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
  auto ret = std::shared_ptr<WsConnectionContext>(
      new WsConnectionContextImpl(lws_ctx));
  return ret;
}

WsConnectionContextImpl::WsConnectionContextImpl(struct lws_context* lws_ctx)
    : lws_context_(lws_ctx) {
  Start();
}

WsConnectionContextImpl::~WsConnectionContextImpl() {
  lws_context_destroy(lws_context_);
  if (message_loop_.joinable()) message_loop_.join();
}

void WsConnectionContextImpl::Start() {
  message_loop_ = std::thread([this]() {
    for (;;) {
      if (lws_service(lws_context_, 0) < 0) {
        break;
      }
    }
  });
}

std::shared_ptr<WsConnection> WsConnectionContextImpl::CreateConnection(
    int port, const std::string& addr, const std::string& path,
    WsConnection::Security security,
    std::weak_ptr<WsConnectionObserver> observer) {
  std::shared_ptr<WsConnection> ret(new WsConnectionImpl(
      port, addr, path, security, observer, shared_from_this()));
  return ret;
}

std::shared_ptr<WsConnectionImpl> WsConnectionContextImpl::GetConnection(
    void* raw) {
  std::shared_ptr<WsConnectionImpl> connection;
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

void WsConnectionContextImpl::RememberConnection(
    void* raw, std::weak_ptr<WsConnectionImpl> conn) {
  std::lock_guard<std::mutex> lock(map_mutex_);
  weak_by_ptr_.emplace(
      std::pair<void*, std::weak_ptr<WsConnectionImpl>>(raw, conn));
}

void WsConnectionContextImpl::ForgetConnection(void* raw) {
  std::lock_guard<std::mutex> lock(map_mutex_);
  weak_by_ptr_.erase(raw);
}

WsConnectionImpl::WsConnectionImpl(
    int port, const std::string& addr, const std::string& path,
    Security security, std::weak_ptr<WsConnectionObserver> observer,
    std::shared_ptr<WsConnectionContextImpl> context)
    : port_(port),
      addr_(addr),
      path_(path),
      security_(security),
      observer_(observer),
      context_(context) {}

WsConnectionImpl::~WsConnectionImpl() {
  context_->ForgetConnection(this);
  // This will cause the callback to be called which will drop the connection
  // after seeing the context doesn't remember this object
  lws_callback_on_writable(wsi_);
}

void WsConnectionImpl::Connect() {
  memset(&extended_sul_.sul, 0, sizeof(extended_sul_.sul));
  extended_sul_.weak_this = weak_from_this();
  lws_sul_schedule(context_->lws_context(), 0, &extended_sul_.sul,
                   CreateConnectionCallback, 1);
}

void WsConnectionImpl::OnError(const std::string& error) {
  auto observer = observer_.lock();
  if (observer) {
    observer->OnError(error);
  }
}
void WsConnectionImpl::OnReceive(const uint8_t* data, size_t len,
                                 bool is_binary) {
  auto observer = observer_.lock();
  if (observer) {
    observer->OnReceive(data, len, is_binary);
  }
}
void WsConnectionImpl::OnOpen() {
  auto observer = observer_.lock();
  if (observer) {
    observer->OnOpen();
  }
}
void WsConnectionImpl::OnClose() {
  auto observer = observer_.lock();
  if (observer) {
    observer->OnClose();
  }
}

void WsConnectionImpl::OnWriteable() {
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

bool WsConnectionImpl::Send(const uint8_t* data, size_t len, bool binary) {
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
      [wsi, user](std::function<void(std::shared_ptr<WsConnectionImpl>)> cb) {
        auto context = reinterpret_cast<WsConnectionContextImpl*>(user);
        auto connection = context->GetConnection(wsi);
        if (!connection) {
          return DROP;
        }
        cb(connection);
        return OK;
      };

  switch (reason) {
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      return with_connection(
          [in](std::shared_ptr<WsConnectionImpl> connection) {
            connection->OnError(in ? (char*)in : "(null)");
          });

    case LWS_CALLBACK_CLIENT_RECEIVE:
      return with_connection(
          [in, len, wsi](std::shared_ptr<WsConnectionImpl> connection) {
            connection->OnReceive((const uint8_t*)in, len,
                                  lws_frame_is_binary(wsi));
          });

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      return with_connection([](std::shared_ptr<WsConnectionImpl> connection) {
        connection->OnOpen();
      });

    case LWS_CALLBACK_CLIENT_CLOSED:
      return with_connection([](std::shared_ptr<WsConnectionImpl> connection) {
        connection->OnClose();
      });

    case LWS_CALLBACK_CLIENT_WRITEABLE:
      return with_connection([](std::shared_ptr<WsConnectionImpl> connection) {
        connection->OnWriteable();
      });

    default:
      LOG(VERBOSE) << "Unhandled value: " << reason;
      return lws_callback_http_dummy(wsi, reason, user, in, len);
  }
}

void CreateConnectionCallback(lws_sorted_usec_list_t* sul) {
  std::shared_ptr<WsConnectionImpl> connection =
      reinterpret_cast<WsConnectionImpl::CreateConnectionSul*>(sul)
          ->weak_this.lock();
  if (!connection) {
    LOG(WARNING) << "The object was already destroyed by the time of the first "
                 << "connection attempt. That's unusual.";
    return;
  }
  connection->ConnectInner();
}

void WsConnectionImpl::ConnectInner() {
  struct lws_client_connect_info connect_info;

  memset(&connect_info, 0, sizeof(connect_info));

  connect_info.context = context_->lws_context();
  connect_info.port = port_;
  connect_info.address = addr_.c_str();
  connect_info.path = path_.c_str();
  connect_info.host = connect_info.address;
  connect_info.origin = connect_info.address;
  switch (security_) {
    case Security::kAllowSelfSigned:
      connect_info.ssl_connection = LCCSCF_ALLOW_SELFSIGNED |
                                    LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK |
                                    LCCSCF_USE_SSL;
      break;
    case Security::kStrict:
      connect_info.ssl_connection = LCCSCF_USE_SSL;
      break;
    case Security::kInsecure:
      connect_info.ssl_connection = 0;
      break;
  }
  connect_info.protocol = "UNNUSED";
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
