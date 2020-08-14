/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "host/libs/config/known_paths.h"

#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

std::string AdbConnectorBinary() {
  return DefaultHostArtifactsPath("bin/adb_connector");
}

std::string ConfigServerBinary() {
  return DefaultHostArtifactsPath("bin/config_server");
}

std::string ConsoleForwarderBinary() {
  return DefaultHostArtifactsPath("bin/console_forwarder");
}

std::string KernelLogMonitorBinary() {
  return DefaultHostArtifactsPath("bin/kernel_log_monitor");
}

std::string LogcatReceiverBinary() {
  return DefaultHostArtifactsPath("bin/logcat_receiver");
}

std::string ModemSimulatorBinary() {
  return DefaultHostArtifactsPath("bin/modem_simulator");
}

std::string SocketVsockProxyBinary() {
  return DefaultHostArtifactsPath("bin/socket_vsock_proxy");
}

std::string WebRtcBinary() {
  return DefaultHostArtifactsPath("bin/webRTC");
}

std::string WebRtcSigServerBinary() {
  return DefaultHostArtifactsPath("bin/webrtc_operator");
}

std::string VncServerBinary() {
  return DefaultHostArtifactsPath("bin/vnc_server");
}

} // namespace cuttlefish
