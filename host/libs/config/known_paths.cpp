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

#include "host/libs/config/config_utils.h"

namespace cuttlefish {

std::string AdbConnectorBinary() {
  return HostBinaryPath("adb_connector");
}

std::string AvbToolBinary() { return HostBinaryPath("avbtool"); }

std::string CasimirControlServerBinary() {
  return HostBinaryPath("casimir_control_server");
}

std::string ConsoleForwarderBinary() {
  return HostBinaryPath("console_forwarder");
}

std::string ControlEnvProxyServerBinary() {
  return HostBinaryPath("control_env_proxy_server");
}

std::string EchoServerBinary() { return HostBinaryPath("echo_server"); }

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

std::string NetsimdBinary() { return HostBinaryPath("netsimd"); }

std::string OpenwrtControlServerBinary() {
  return HostBinaryPath("openwrt_control_server");
}

std::string PicaBinary() { return HostBinaryPath("pica"); }

std::string ProcessRestarterBinary() {
  return HostBinaryPath("process_restarter");
}

std::string RootCanalBinary() { return HostBinaryPath("root-canal"); }

std::string TestKeyRsa2048() {
  return DefaultHostArtifactsPath("etc/cvd_avb_testkey_rsa2048.pem");
}

std::string TestKeyRsa4096() {
  return DefaultHostArtifactsPath("etc/cvd_avb_testkey_rsa4096.pem");
}

std::string TestPubKeyRsa2048() {
  return DefaultHostArtifactsPath("etc/cvd_rsa2048.avbpubkey");
}

std::string TestPubKeyRsa4096() {
  return DefaultHostArtifactsPath("etc/cvd_rsa4096.avbpubkey");
}

std::string CasimirBinary() { return HostBinaryPath("casimir"); }

std::string ScreenRecordingServerBinary() {
  return HostBinaryPath("screen_recording_server");
}

std::string SecureEnvBinary() { return HostBinaryPath("secure_env"); }

std::string SocketVsockProxyBinary() {
  return HostBinaryPath("socket_vsock_proxy");
}

std::string StopCvdBinary() { return HostBinaryPath("stop_cvd"); }

std::string TcpConnectorBinary() { return HostBinaryPath("tcp_connector"); }

std::string TombstoneReceiverBinary() {
  return HostBinaryPath("tombstone_receiver");
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

std::string AutomotiveProxyBinary() {
  return HostBinaryPath("automotive_vsock_proxy");
}

} // namespace cuttlefish
