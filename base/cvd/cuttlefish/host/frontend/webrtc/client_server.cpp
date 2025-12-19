//
// Copyright (C) 2021 The Android Open Source Project
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
// limitations under the License.

#include "cuttlefish/host/frontend/webrtc/client_server.h"
#include <android-base/logging.h>

namespace cuttlefish {
struct ClientFilesServer::Config {
  Config(const std::string& dir)
      : dir_(dir),
        mount_({
            .mount_next = nullptr,  /* linked-list "next" */
            .mountpoint = "/",      /* mountpoint URL */
            .origin = dir_.c_str(), /* serve from dir */
            .def = "client.html",   /* default filename */
            .protocol = nullptr,
            .cgienv = nullptr,
            .extra_mimetypes = nullptr,
            .interpret = nullptr,
            .cgi_timeout = 0,
            .cache_max_age = 0,
            .auth_mask = 0,
            .cache_reusable = 0,
            .cache_revalidate = 0,
            .cache_intermediaries = 0,
            .origin_protocol = LWSMPRO_FILE, /* files in a dir */
            .mountpoint_len = 1,             /* char count */
            .basic_auth_login_file = nullptr,
        }) {
    memset(&info_, 0, sizeof info_);
    info_.port = 0;             // let the kernel select an available port
    info_.iface = "127.0.0.1";  // listen only on localhost
    info_.options = LWS_SERVER_OPTION_EXPLICIT_VHOSTS;
    info_.mounts = &mount_;
  }

  std::string dir_;
  lws_http_mount mount_;
  lws_context_creation_info info_;
};

ClientFilesServer::ClientFilesServer(std::unique_ptr<Config> config,
                                     lws_context* context, lws_vhost* vhost)
    : config_(std::move(config)),
      context_(context),
      vhost_(vhost),
      running_(true),
      server_thread_([this]() { Serve(); }) {}

ClientFilesServer::~ClientFilesServer() {
  if (running_) {
    running_ = false;
    lws_cancel_service(context_);
    server_thread_.join();
  }
  if (context_) {
    // Release the port and other resources
    lws_vhost_destroy(vhost_);
    lws_context_destroy(context_);
  }
}

std::unique_ptr<ClientFilesServer> ClientFilesServer::New(
    const std::string& dir) {
  std::unique_ptr<Config> conf(new Config(dir));
  if (!conf) {
    return nullptr;
  }

  auto ctx = lws_create_context(&conf->info_);
  if (!ctx) {
    LOG(ERROR) << "Failed to create lws context";
    return nullptr;
  }
  auto vhost = lws_create_vhost(ctx, &conf->info_);
  if (!vhost) {
    LOG(ERROR) << "Failed to create lws vhost";
    lws_context_destroy(ctx);
    return nullptr;
  }
  return std::unique_ptr<ClientFilesServer>(
      new ClientFilesServer(std::move(conf), ctx, vhost));
}

int ClientFilesServer::port() const {
  // Get the port for the first (and only) vhost.
  return lws_get_vhost_listen_port(vhost_);
}

void ClientFilesServer::Serve() {
  while (running_) {
    // Set a large timeout so it doesn't unnecessarily wake the thread too
    // frequently. Newer versions of libwebsockets ignore this value and only
    // return once some action was taken, but older ones respect it and there is
    // no way to tell them to wait indefinitely.
    int poll_timeout_ms = 1000000;
    if (lws_service(context_, poll_timeout_ms) < 0) {
      LOG(ERROR) << "Error serving client files";
      return;
    }
  }
}
}  // namespace cuttlefish

