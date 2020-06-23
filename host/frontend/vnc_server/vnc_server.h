#pragma once

/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "common/libs/tcp_socket/tcp_socket.h"
#include "host/frontend/vnc_server/blackboard.h"
#include "host/frontend/vnc_server/frame_buffer_watcher.h"
#include "host/frontend/vnc_server/jpeg_compressor.h"
#include "host/frontend/vnc_server/virtual_inputs.h"
#include "host/frontend/vnc_server/vnc_client_connection.h"
#include "host/frontend/vnc_server/vnc_utils.h"

namespace cuttlefish {
namespace vnc {

class VncServer {
 public:
  explicit VncServer(int port, bool aggressive);

  VncServer(const VncServer&) = delete;
  VncServer& operator=(const VncServer&) = delete;

  [[noreturn]] void MainLoop();

 private:
  void StartClient(ClientSocket sock);

  void StartClientThread(ClientSocket sock);

  ServerSocket server_;
  std::shared_ptr<VirtualInputs> virtual_inputs_;
  BlackBoard bb_;
  FrameBufferWatcher frame_buffer_watcher_;
  bool aggressive_{};
};

}  // namespace vnc
}  // namespace cuttlefish
