/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android-base/logging.h>
#include <gflags/gflags.h>
#include <libwebsockets.h>

#include "common/libs/utils/files.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/logging.h"

DEFINE_int32(http_server_port, 8443, "The port for the http server");
DEFINE_bool(use_secure_http, true, "Whether to use HTTPS or HTTP.");
DEFINE_string(certs_dir,
              cuttlefish::DefaultHostArtifactsPath("usr/share/webrtc/certs"),
              "Directory to certificates. It must contain a server.crt file, a "
              "server.key file and (optionally) a CA.crt file.");
DEFINE_string(operator_addr, "localhost:1080/",
              "The address of the operator server to proxy");

int main(int argc, char** argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);

  struct lws_context_creation_info info;
  struct lws_context* context;

  lws_set_log_level(LLL_ERR, NULL);

  struct lws_http_mount mount = {
      .mount_next = nullptr,
      .mountpoint = "/",
      .mountpoint_len = static_cast<uint8_t>(1),
      .origin = FLAGS_operator_addr.c_str(),
      .def = nullptr,
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
      .origin_protocol = LWSMPRO_HTTP,  // reverse proxy
      .basic_auth_login_file = nullptr,
  };

  memset(&info, 0, sizeof info);
  info.port = FLAGS_http_server_port;
  info.mounts = &mount;

  // These vars need to be in scope for the call to lws_create-context below
  std::string cert_file = FLAGS_certs_dir + "/server.crt";
  std::string key_file = FLAGS_certs_dir + "/server.key";
  std::string ca_file = FLAGS_certs_dir + "/CA.crt";
  if (FLAGS_use_secure_http) {
    info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.ssl_cert_filepath = cert_file.c_str();
    info.ssl_private_key_filepath = key_file.c_str();
    if (cuttlefish::FileExists(ca_file)) {
      info.ssl_ca_filepath = ca_file.c_str();
    }
  }

  context = lws_create_context(&info);
  CHECK(context) << "Unable to create reverse proxy";
  LOG(VERBOSE) << "Started reverse proxy to signaling server";
  while (lws_service(context, 0) >= 0) {
  }
  lws_context_destroy(context);
  return 0;
}
