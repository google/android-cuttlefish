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

#include "host/frontend/webrtc/client_server.h"
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
    info_.mounts = &mount_;
  }

  std::string dir_;
  lws_http_mount mount_;
  lws_context_creation_info info_;
};

ClientFilesServer::ClientFilesServer(std::unique_ptr<Config> config,
                                     lws_context* context)
    : config_(std::move(config)),
      context_(context),
      running_(true),
      server_thread_([this]() { Serve(); }) {}

ClientFilesServer::~ClientFilesServer() {
  if (running_) {
    running_ = false;
    server_thread_.join();
  }
  if (context_) {
    // Release the port and other resources
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
  return std::unique_ptr<ClientFilesServer>(
      new ClientFilesServer(std::move(conf), ctx));
}

int ClientFilesServer::port() const {
  // Get the port for the first (and only) vhost.
  return lws_get_vhost_listen_port(lws_get_vhost_by_name(context_, "default"));
}

void ClientFilesServer::Serve() {
  while (running_) {
    if (lws_service(context_, 0) < 0) {
      LOG(ERROR) << "Error serving client files";
      return;
    }
  }
}
}  // namespace cuttlefish

