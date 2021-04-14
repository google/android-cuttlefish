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

#include <algorithm>
#include <memory>
#include <string>

#include <gflags/gflags.h>

#include "host/frontend/vnc_server/simulated_hw_composer.h"
#include "host/frontend/vnc_server/vnc_server.h"
#include "host/frontend/vnc_server/vnc_utils.h"
#include "host/libs/config/logging.h"
#include "host/libs/confui/host_mode_ctrl.h"
#include "host/libs/confui/host_server.h"

DEFINE_bool(agressive, false, "Whether to use agressive server");
DEFINE_int32(frame_server_fd, -1, "");
DEFINE_int32(port, 6444, "Port where to listen for connections");

int main(int argc, char* argv[]) {
  cuttlefish::DefaultSubprocessLogging(argv);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto& host_mode_ctrl = cuttlefish::HostModeCtrl::Get();
  auto screen_connector_ptr = cuttlefish::vnc::ScreenConnector::Get(
      FLAGS_frame_server_fd, host_mode_ctrl);
  auto& screen_connector = *(screen_connector_ptr.get());

  // create confirmation UI service, giving host_mode_ctrl and
  // screen_connector
  // keep this singleton object alive until the webRTC process ends
  static auto& host_confui_server =
      cuttlefish::confui::HostServer::Get(host_mode_ctrl, screen_connector);

  host_confui_server.Start();
  // lint does not like the spelling of "agressive", so needs NOTYPO
  cuttlefish::vnc::VncServer vnc_server(FLAGS_port, FLAGS_agressive,  // NOTYPO
                                        screen_connector, host_confui_server);
  vnc_server.MainLoop();
}
