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
  return HostBinaryPath("adb_connector");
}

std::string ConfigServerBinary() {
  return HostBinaryPath("config_server");
}

std::string ConsoleForwarderBinary() {
  return HostBinaryPath("console_forwarder");
}

std::string GnssGrpcProxyBinary() {
  return HostBinaryPath("gnss_grpc_proxy");
}

std::string KernelLogMonitorBinary() {
  return HostBinaryPath("kernel_log_monitor");
}

std::string LogcatReceiverBinary() {
  return HostBinaryPath("logcat_receiver");
}

std::string MetricsBinary() {
  return HostBinaryPath("metrics");
}

std::string ModemSimulatorBinary() {
  return HostBinaryPath("modem_simulator");
}

std::string OpenwrtControlServerBinary() {
  return HostBinaryPath("openwrt_control_server");
}

std::string RootCanalBinary() {
  return HostBinaryPath("root-canal");
}

std::string PicaBinary() {
  return HostBinaryPath("pica");
}

std::string EchoServerBinary() { return HostBinaryPath("echo_server"); }

std::string SocketVsockProxyBinary() {
  return HostBinaryPath("socket_vsock_proxy");
}

std::string TombstoneReceiverBinary() {
  return HostBinaryPath("tombstone_receiver");
}

std::string VehicleHalGrpcServerBinary() {
  return HostBinaryPath(
      "android.hardware.automotive.vehicle@2.0-virtualization-grpc-server");
}

std::string WebRtcBinary() {
  return HostBinaryPath("webRTC");
}

std::string WebRtcSigServerBinary() {
  return HostBinaryPath("webrtc_operator");
}

std::string WebRtcSigServerProxyBinary() {
  return HostBinaryPath("operator_proxy");
}

std::string WmediumdBinary() { return HostBinaryPath("wmediumd"); }

std::string WmediumdGenConfigBinary() {
  return HostBinaryPath("wmediumd_gen_config");
}

} // namespace cuttlefish
