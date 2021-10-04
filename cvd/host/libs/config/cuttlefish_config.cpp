/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "host/libs/config/cuttlefish_config.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <time.h>

#include <android-base/strings.h>
#include <android-base/logging.h>
#include <json/json.h>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"

namespace cuttlefish {
namespace {

int InstanceFromEnvironment() {
  static constexpr char kInstanceEnvironmentVariable[] = "CUTTLEFISH_INSTANCE";
  static constexpr int kDefaultInstance = 1;

  // CUTTLEFISH_INSTANCE environment variable
  std::string instance_str = StringFromEnv(kInstanceEnvironmentVariable, "");
  if (instance_str.empty()) {
    // Try to get it from the user instead
    instance_str = StringFromEnv("USER", "");

    if (instance_str.empty()) {
      LOG(DEBUG) << "CUTTLEFISH_INSTANCE and USER unset, using instance id "
                 << kDefaultInstance;
      return kDefaultInstance;
    }
    if (!android::base::StartsWith(instance_str, kVsocUserPrefix)) {
      // No user or we don't recognize this user
      LOG(DEBUG) << "Non-vsoc user, using instance id " << kDefaultInstance;
      return kDefaultInstance;
    }
    instance_str = instance_str.substr(std::string(kVsocUserPrefix).size());
  }
  int instance = std::stoi(instance_str);
  if (instance <= 0) {
    LOG(INFO) << "Failed to interpret \"" << instance_str << "\" as an id, "
              << "using instance id " << kDefaultInstance;
    return kDefaultInstance;
  }
  return instance;
}

const char* kInstances = "instances";


}  // namespace

const char* const kGpuModeAuto = "auto";
const char* const kGpuModeGuestSwiftshader = "guest_swiftshader";
const char* const kGpuModeDrmVirgl = "drm_virgl";
const char* const kGpuModeGfxStream = "gfxstream";

std::string DefaultEnvironmentPath(const char* environment_key,
                                   const char* default_value,
                                   const char* subpath) {
  return StringFromEnv(environment_key, default_value) + "/" + subpath;
}

static constexpr char kAssemblyDir[] = "assembly_dir";
std::string CuttlefishConfig::assembly_dir() const {
  return (*dictionary_)[kAssemblyDir].asString();
}
void CuttlefishConfig::set_assembly_dir(const std::string& assembly_dir) {
  (*dictionary_)[kAssemblyDir] = assembly_dir;
}

static constexpr char kVmManager[] = "vm_manager";
std::string CuttlefishConfig::vm_manager() const {
  return (*dictionary_)[kVmManager].asString();
}
void CuttlefishConfig::set_vm_manager(const std::string& name) {
  (*dictionary_)[kVmManager] = name;
}

static constexpr char kGpuMode[] = "gpu_mode";
std::string CuttlefishConfig::gpu_mode() const {
  return (*dictionary_)[kGpuMode].asString();
}
void CuttlefishConfig::set_gpu_mode(const std::string& name) {
  (*dictionary_)[kGpuMode] = name;
}

static constexpr char kCpus[] = "cpus";
int CuttlefishConfig::cpus() const { return (*dictionary_)[kCpus].asInt(); }
void CuttlefishConfig::set_cpus(int cpus) { (*dictionary_)[kCpus] = cpus; }

static constexpr char kMemoryMb[] = "memory_mb";
int CuttlefishConfig::memory_mb() const {
  return (*dictionary_)[kMemoryMb].asInt();
}
void CuttlefishConfig::set_memory_mb(int memory_mb) {
  (*dictionary_)[kMemoryMb] = memory_mb;
}

static constexpr char kDpi[] = "dpi";
int CuttlefishConfig::dpi() const { return (*dictionary_)[kDpi].asInt(); }
void CuttlefishConfig::set_dpi(int dpi) { (*dictionary_)[kDpi] = dpi; }

static constexpr char kDisplayConfigs[] = "display_configs";
static constexpr char kXRes[] = "x_res";
static constexpr char kYRes[] = "y_res";
std::vector<CuttlefishConfig::DisplayConfig>
CuttlefishConfig::display_configs() const {
  std::vector<DisplayConfig> display_configs;
  for (auto& display_config_json : (*dictionary_)[kDisplayConfigs]) {
    DisplayConfig display_config = {};
    display_config.width = display_config_json[kXRes].asInt();
    display_config.height = display_config_json[kYRes].asInt();
    display_configs.emplace_back(std::move(display_config));
  }
  return display_configs;
}
void CuttlefishConfig::set_display_configs(
    const std::vector<DisplayConfig>& display_configs) {
  Json::Value display_configs_json(Json::arrayValue);

  for (const DisplayConfig& display_configs : display_configs) {
    Json::Value display_config_json(Json::objectValue);
    display_config_json[kXRes] = display_configs.width;
    display_config_json[kYRes] = display_configs.height;
    display_configs_json.append(display_config_json);
  }

  (*dictionary_)[kDisplayConfigs] = display_configs_json;
}

static constexpr char kRefreshRateHz[] = "refresh_rate_hz";
int CuttlefishConfig::refresh_rate_hz() const {
  return (*dictionary_)[kRefreshRateHz].asInt();
}
void CuttlefishConfig::set_refresh_rate_hz(int refresh_rate_hz) {
  (*dictionary_)[kRefreshRateHz] = refresh_rate_hz;
}

void CuttlefishConfig::SetPath(const std::string& key,
                               const std::string& path) {
  if (!path.empty()) {
    (*dictionary_)[key] = AbsolutePath(path);
  }
}

static constexpr char kGdbPort[] = "gdb_port";
int CuttlefishConfig::gdb_port() const {
  return (*dictionary_)[kGdbPort].asInt();
}
void CuttlefishConfig::set_gdb_port(int port) {
  (*dictionary_)[kGdbPort] = port;
}

static constexpr char kDeprecatedBootCompleted[] = "deprecated_boot_completed";
bool CuttlefishConfig::deprecated_boot_completed() const {
  return (*dictionary_)[kDeprecatedBootCompleted].asBool();
}
void CuttlefishConfig::set_deprecated_boot_completed(
    bool deprecated_boot_completed) {
  (*dictionary_)[kDeprecatedBootCompleted] = deprecated_boot_completed;
}

static constexpr char kCuttlefishEnvPath[] = "cuttlefish_env_path";
void CuttlefishConfig::set_cuttlefish_env_path(const std::string& path) {
  SetPath(kCuttlefishEnvPath, path);
}
std::string CuttlefishConfig::cuttlefish_env_path() const {
  return (*dictionary_)[kCuttlefishEnvPath].asString();
}

static AdbMode stringToAdbMode(std::string mode) {
  std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
  if (mode == "vsock_tunnel") {
    return AdbMode::VsockTunnel;
  } else if (mode == "vsock_half_tunnel") {
    return AdbMode::VsockHalfTunnel;
  } else if (mode == "native_vsock") {
    return AdbMode::NativeVsock;
  } else {
    return AdbMode::Unknown;
  }
}

static constexpr char kAdbMode[] = "adb_mode";
std::set<AdbMode> CuttlefishConfig::adb_mode() const {
  std::set<AdbMode> args_set;
  for (auto& mode : (*dictionary_)[kAdbMode]) {
    args_set.insert(stringToAdbMode(mode.asString()));
  }
  return args_set;
}
void CuttlefishConfig::set_adb_mode(const std::set<std::string>& mode) {
  Json::Value mode_json_obj(Json::arrayValue);
  for (const auto& arg : mode) {
    mode_json_obj.append(arg);
  }
  (*dictionary_)[kAdbMode] = mode_json_obj;
}

static SecureHal StringToSecureHal(std::string mode) {
  std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
  if (mode == "keymint") {
    return SecureHal::Keymint;
  } else if (mode == "gatekeeper") {
    return SecureHal::Gatekeeper;
  } else {
    return SecureHal::Unknown;
  }
}

static constexpr char kSecureHals[] = "secure_hals";
std::set<SecureHal> CuttlefishConfig::secure_hals() const {
  std::set<SecureHal> args_set;
  for (auto& hal : (*dictionary_)[kSecureHals]) {
    args_set.insert(StringToSecureHal(hal.asString()));
  }
  return args_set;
}
void CuttlefishConfig::set_secure_hals(const std::set<std::string>& hals) {
  Json::Value hals_json_obj(Json::arrayValue);
  for (const auto& hal : hals) {
    hals_json_obj.append(hal);
  }
  (*dictionary_)[kSecureHals] = hals_json_obj;
}

static constexpr char kSetupWizardMode[] = "setupwizard_mode";
std::string CuttlefishConfig::setupwizard_mode() const {
  return (*dictionary_)[kSetupWizardMode].asString();
}
void CuttlefishConfig::set_setupwizard_mode(const std::string& mode) {
  (*dictionary_)[kSetupWizardMode] = mode;
}

static constexpr char kQemuBinaryDir[] = "qemu_binary_dir";
std::string CuttlefishConfig::qemu_binary_dir() const {
  return (*dictionary_)[kQemuBinaryDir].asString();
}
void CuttlefishConfig::set_qemu_binary_dir(const std::string& qemu_binary_dir) {
  (*dictionary_)[kQemuBinaryDir] = qemu_binary_dir;
}

static constexpr char kCrosvmBinary[] = "crosvm_binary";
std::string CuttlefishConfig::crosvm_binary() const {
  return (*dictionary_)[kCrosvmBinary].asString();
}
void CuttlefishConfig::set_crosvm_binary(const std::string& crosvm_binary) {
  (*dictionary_)[kCrosvmBinary] = crosvm_binary;
}

static constexpr char kTpmDevice[] = "tpm_device";
std::string CuttlefishConfig::tpm_device() const {
  return (*dictionary_)[kTpmDevice].asString();
}
void CuttlefishConfig::set_tpm_device(const std::string& tpm_device) {
  (*dictionary_)[kTpmDevice] = tpm_device;
}

static constexpr char kEnableGnssGrpcProxy[] = "enable_gnss_grpc_proxy";
void CuttlefishConfig::set_enable_gnss_grpc_proxy(const bool enable_gnss_grpc_proxy) {
  (*dictionary_)[kEnableGnssGrpcProxy] = enable_gnss_grpc_proxy;
}
bool CuttlefishConfig::enable_gnss_grpc_proxy() const {
  return (*dictionary_)[kEnableGnssGrpcProxy].asBool();
}

static constexpr char kEnableVncServer[] = "enable_vnc_server";
bool CuttlefishConfig::enable_vnc_server() const {
  return (*dictionary_)[kEnableVncServer].asBool();
}
void CuttlefishConfig::set_enable_vnc_server(bool enable_vnc_server) {
  (*dictionary_)[kEnableVncServer] = enable_vnc_server;
}

static constexpr char kEnableSandbox[] = "enable_sandbox";
void CuttlefishConfig::set_enable_sandbox(const bool enable_sandbox) {
  (*dictionary_)[kEnableSandbox] = enable_sandbox;
}
bool CuttlefishConfig::enable_sandbox() const {
  return (*dictionary_)[kEnableSandbox].asBool();
}

static constexpr char kSeccompPolicyDir[] = "seccomp_policy_dir";
void CuttlefishConfig::set_seccomp_policy_dir(const std::string& seccomp_policy_dir) {
  if (seccomp_policy_dir.empty()) {
    (*dictionary_)[kSeccompPolicyDir] = seccomp_policy_dir;
    return;
  }
  SetPath(kSeccompPolicyDir, seccomp_policy_dir);
}
std::string CuttlefishConfig::seccomp_policy_dir() const {
  return (*dictionary_)[kSeccompPolicyDir].asString();
}

static constexpr char kEnableWebRTC[] = "enable_webrtc";
void CuttlefishConfig::set_enable_webrtc(bool enable_webrtc) {
  (*dictionary_)[kEnableWebRTC] = enable_webrtc;
}
bool CuttlefishConfig::enable_webrtc() const {
  return (*dictionary_)[kEnableWebRTC].asBool();
}

static constexpr char kEnableVehicleHalServer[] = "enable_vehicle_hal_server";
void CuttlefishConfig::set_enable_vehicle_hal_grpc_server(bool enable_vehicle_hal_grpc_server) {
  (*dictionary_)[kEnableVehicleHalServer] = enable_vehicle_hal_grpc_server;
}
bool CuttlefishConfig::enable_vehicle_hal_grpc_server() const {
  return (*dictionary_)[kEnableVehicleHalServer].asBool();
}

static constexpr char kVehicleHalServerBinary[] = "vehicle_hal_server_binary";
void CuttlefishConfig::set_vehicle_hal_grpc_server_binary(const std::string& vehicle_hal_server_binary) {
  (*dictionary_)[kVehicleHalServerBinary] = vehicle_hal_server_binary;
}
std::string CuttlefishConfig::vehicle_hal_grpc_server_binary() const {
  return (*dictionary_)[kVehicleHalServerBinary].asString();
}

static constexpr char kCustomActions[] = "custom_actions";
void CuttlefishConfig::set_custom_actions(const std::vector<CustomActionConfig>& actions) {
  Json::Value actions_array(Json::arrayValue);
  for (const auto& action : actions) {
    actions_array.append(action.ToJson());
  }
  (*dictionary_)[kCustomActions] = actions_array;
}
std::vector<CustomActionConfig> CuttlefishConfig::custom_actions() const {
  std::vector<CustomActionConfig> result;
  for (Json::Value custom_action : (*dictionary_)[kCustomActions]) {
    result.push_back(CustomActionConfig(custom_action));
  }
  return result;
}

static constexpr char kWebRTCAssetsDir[] = "webrtc_assets_dir";
void CuttlefishConfig::set_webrtc_assets_dir(const std::string& webrtc_assets_dir) {
  (*dictionary_)[kWebRTCAssetsDir] = webrtc_assets_dir;
}
std::string CuttlefishConfig::webrtc_assets_dir() const {
  return (*dictionary_)[kWebRTCAssetsDir].asString();
}

static constexpr char kWebRTCEnableADBWebSocket[] =
    "webrtc_enable_adb_websocket";
void CuttlefishConfig::set_webrtc_enable_adb_websocket(bool enable) {
    (*dictionary_)[kWebRTCEnableADBWebSocket] = enable;
}
bool CuttlefishConfig::webrtc_enable_adb_websocket() const {
    return (*dictionary_)[kWebRTCEnableADBWebSocket].asBool();
}

static constexpr char kRestartSubprocesses[] = "restart_subprocesses";
bool CuttlefishConfig::restart_subprocesses() const {
  return (*dictionary_)[kRestartSubprocesses].asBool();
}
void CuttlefishConfig::set_restart_subprocesses(bool restart_subprocesses) {
  (*dictionary_)[kRestartSubprocesses] = restart_subprocesses;
}

static constexpr char kRunAdbConnector[] = "run_adb_connector";
bool CuttlefishConfig::run_adb_connector() const {
  return (*dictionary_)[kRunAdbConnector].asBool();
}
void CuttlefishConfig::set_run_adb_connector(bool run_adb_connector) {
  (*dictionary_)[kRunAdbConnector] = run_adb_connector;
}

static constexpr char kRunAsDaemon[] = "run_as_daemon";
bool CuttlefishConfig::run_as_daemon() const {
  return (*dictionary_)[kRunAsDaemon].asBool();
}
void CuttlefishConfig::set_run_as_daemon(bool run_as_daemon) {
  (*dictionary_)[kRunAsDaemon] = run_as_daemon;
}

static constexpr char kDataPolicy[] = "data_policy";
std::string CuttlefishConfig::data_policy() const {
  return (*dictionary_)[kDataPolicy].asString();
}
void CuttlefishConfig::set_data_policy(const std::string& data_policy) {
  (*dictionary_)[kDataPolicy] = data_policy;
}

static constexpr char kBlankDataImageMb[] = "blank_data_image_mb";
int CuttlefishConfig::blank_data_image_mb() const {
  return (*dictionary_)[kBlankDataImageMb].asInt();
}
void CuttlefishConfig::set_blank_data_image_mb(int blank_data_image_mb) {
  (*dictionary_)[kBlankDataImageMb] = blank_data_image_mb;
}

static constexpr char kBlankDataImageFmt[] = "blank_data_image_fmt";
std::string CuttlefishConfig::blank_data_image_fmt() const {
  return (*dictionary_)[kBlankDataImageFmt].asString();
}
void CuttlefishConfig::set_blank_data_image_fmt(const std::string& blank_data_image_fmt) {
  (*dictionary_)[kBlankDataImageFmt] = blank_data_image_fmt;
}

static constexpr char kBootloader[] = "bootloader";
std::string CuttlefishConfig::bootloader() const {
  return (*dictionary_)[kBootloader].asString();
}
void CuttlefishConfig::set_bootloader(const std::string& bootloader) {
  SetPath(kBootloader, bootloader);
}

static constexpr char kBootSlot[] = "boot_slot";
void CuttlefishConfig::set_boot_slot(const std::string& boot_slot) {
  (*dictionary_)[kBootSlot] = boot_slot;
}
std::string CuttlefishConfig::boot_slot() const {
  return (*dictionary_)[kBootSlot].asString();
}

static constexpr char kWebRTCCertsDir[] = "webrtc_certs_dir";
void CuttlefishConfig::set_webrtc_certs_dir(const std::string& certs_dir) {
  (*dictionary_)[kWebRTCCertsDir] = certs_dir;
}
std::string CuttlefishConfig::webrtc_certs_dir() const {
  return (*dictionary_)[kWebRTCCertsDir].asString();
}

static constexpr char kSigServerPort[] = "webrtc_sig_server_port";
void CuttlefishConfig::set_sig_server_port(int port) {
  (*dictionary_)[kSigServerPort] = port;
}
int CuttlefishConfig::sig_server_port() const {
  return (*dictionary_)[kSigServerPort].asInt();
}

static constexpr char kWebrtcUdpPortRange[] = "webrtc_udp_port_range";
void CuttlefishConfig::set_webrtc_udp_port_range(
    std::pair<uint16_t, uint16_t> range) {
  Json::Value arr(Json::ValueType::arrayValue);
  arr[0] = range.first;
  arr[1] = range.second;
  (*dictionary_)[kWebrtcUdpPortRange] = arr;
}
std::pair<uint16_t, uint16_t> CuttlefishConfig::webrtc_udp_port_range() const {
  std::pair<uint16_t, uint16_t> ret;
  ret.first = (*dictionary_)[kWebrtcUdpPortRange][0].asInt();
  ret.second = (*dictionary_)[kWebrtcUdpPortRange][1].asInt();
  return ret;
}

static constexpr char kWebrtcTcpPortRange[] = "webrtc_tcp_port_range";
void CuttlefishConfig::set_webrtc_tcp_port_range(
    std::pair<uint16_t, uint16_t> range) {
  Json::Value arr(Json::ValueType::arrayValue);
  arr[0] = range.first;
  arr[1] = range.second;
  (*dictionary_)[kWebrtcTcpPortRange] = arr;
}
std::pair<uint16_t, uint16_t> CuttlefishConfig::webrtc_tcp_port_range() const {
  std::pair<uint16_t, uint16_t> ret;
  ret.first = (*dictionary_)[kWebrtcTcpPortRange][0].asInt();
  ret.second = (*dictionary_)[kWebrtcTcpPortRange][1].asInt();
  return ret;
}

static constexpr char kSigServerAddress[] = "webrtc_sig_server_addr";
void CuttlefishConfig::set_sig_server_address(const std::string& addr) {
  (*dictionary_)[kSigServerAddress] = addr;
}
std::string CuttlefishConfig::sig_server_address() const {
  return (*dictionary_)[kSigServerAddress].asString();
}

static constexpr char kSigServerPath[] = "webrtc_sig_server_path";
void CuttlefishConfig::set_sig_server_path(const std::string& path) {
  // Don't use SetPath here, it's a URL path not a file system path
  (*dictionary_)[kSigServerPath] = path;
}
std::string CuttlefishConfig::sig_server_path() const {
  return (*dictionary_)[kSigServerPath].asString();
}

static constexpr char kSigServerStrict[] = "webrtc_sig_server_strict";
void CuttlefishConfig::set_sig_server_strict(bool strict) {
  (*dictionary_)[kSigServerStrict] = strict;
}
bool CuttlefishConfig::sig_server_strict() const {
  return (*dictionary_)[kSigServerStrict].asBool();
}

static constexpr char kSigServerHeadersPath[] =
    "webrtc_sig_server_headers_path";
void CuttlefishConfig::set_sig_server_headers_path(const std::string& path) {
  SetPath(kSigServerHeadersPath, path);
}
std::string CuttlefishConfig::sig_server_headers_path() const {
  return (*dictionary_)[kSigServerHeadersPath].asString();
}

static constexpr char kRunModemSimulator[] = "enable_modem_simulator";
bool CuttlefishConfig::enable_modem_simulator() const {
  return (*dictionary_)[kRunModemSimulator].asBool();
}
void CuttlefishConfig::set_enable_modem_simulator(bool enable_modem_simulator) {
  (*dictionary_)[kRunModemSimulator] = enable_modem_simulator;
}

static constexpr char kModemSimulatorInstanceNumber[] =
    "modem_simulator_instance_number";
void CuttlefishConfig::set_modem_simulator_instance_number(
    int instance_number) {
  (*dictionary_)[kModemSimulatorInstanceNumber] = instance_number;
}
int CuttlefishConfig::modem_simulator_instance_number() const {
  return (*dictionary_)[kModemSimulatorInstanceNumber].asInt();
}

static constexpr char kModemSimulatorSimType[] = "modem_simulator_sim_type";
void CuttlefishConfig::set_modem_simulator_sim_type(int sim_type) {
  (*dictionary_)[kModemSimulatorSimType] = sim_type;
}
int CuttlefishConfig::modem_simulator_sim_type() const {
  return (*dictionary_)[kModemSimulatorSimType].asInt();
}

static constexpr char kHostToolsVersion[] = "host_tools_version";
void CuttlefishConfig::set_host_tools_version(
    const std::map<std::string, uint32_t>& versions) {
  Json::Value json(Json::objectValue);
  for (const auto& [key, value] : versions) {
    json[key] = value;
  }
  (*dictionary_)[kHostToolsVersion] = json;
}
std::map<std::string, uint32_t> CuttlefishConfig::host_tools_version() const {
  if (!dictionary_->isMember(kHostToolsVersion)) {
    return {};
  }
  std::map<std::string, uint32_t> versions;
  const auto& elem = (*dictionary_)[kHostToolsVersion];
  for (auto it = elem.begin(); it != elem.end(); it++) {
    versions[it.key().asString()] = it->asUInt();
  }
  return versions;
}

static constexpr char kGuestEnforceSecurity[] = "guest_enforce_security";
void CuttlefishConfig::set_guest_enforce_security(bool guest_enforce_security) {
  (*dictionary_)[kGuestEnforceSecurity] = guest_enforce_security;
}
bool CuttlefishConfig::guest_enforce_security() const {
  return (*dictionary_)[kGuestEnforceSecurity].asBool();
}

const char* kGuestAuditSecurity = "guest_audit_security";
void CuttlefishConfig::set_guest_audit_security(bool guest_audit_security) {
  (*dictionary_)[kGuestAuditSecurity] = guest_audit_security;
}
bool CuttlefishConfig::guest_audit_security() const {
  return (*dictionary_)[kGuestAuditSecurity].asBool();
}

static constexpr char kenableHostBluetooth[] = "enable_host_bluetooth";
void CuttlefishConfig::set_enable_host_bluetooth(bool enable_host_bluetooth) {
  (*dictionary_)[kenableHostBluetooth] = enable_host_bluetooth;
}
bool CuttlefishConfig::enable_host_bluetooth() const {
  return (*dictionary_)[kenableHostBluetooth].asBool();
}

static constexpr char kEnableMetrics[] = "enable_metrics";
void CuttlefishConfig::set_enable_metrics(std::string enable_metrics) {
  (*dictionary_)[kEnableMetrics] = kUnknown;
  if (!enable_metrics.empty()) {
    switch (enable_metrics.at(0)) {
      case 'y':
      case 'Y':
        (*dictionary_)[kEnableMetrics] = kYes;
        break;
      case 'n':
      case 'N':
        (*dictionary_)[kEnableMetrics] = kNo;
        break;
    }
  }
}
CuttlefishConfig::Answer CuttlefishConfig::enable_metrics() const {
  return (CuttlefishConfig::Answer)(*dictionary_)[kEnableMetrics].asInt();
}

static constexpr char kMetricsBinary[] = "metrics_binary";
void CuttlefishConfig::set_metrics_binary(const std::string& metrics_binary) {
  (*dictionary_)[kMetricsBinary] = metrics_binary;
}
std::string CuttlefishConfig::metrics_binary() const {
  return (*dictionary_)[kMetricsBinary].asString();
}

static constexpr char kExtraKernelCmdline[] = "extra_kernel_cmdline";
void CuttlefishConfig::set_extra_kernel_cmdline(std::string extra_cmdline) {
  Json::Value args_json_obj(Json::arrayValue);
  for (const auto& arg : android::base::Split(extra_cmdline, " ")) {
    args_json_obj.append(arg);
  }
  (*dictionary_)[kExtraKernelCmdline] = args_json_obj;
}
std::vector<std::string> CuttlefishConfig::extra_kernel_cmdline() const {
  std::vector<std::string> cmdline;
  for (const Json::Value& arg : (*dictionary_)[kExtraKernelCmdline]) {
    cmdline.push_back(arg.asString());
  }
  return cmdline;
}

static constexpr char kRilDns[] = "ril_dns";
void CuttlefishConfig::set_ril_dns(const std::string& ril_dns) {
  (*dictionary_)[kRilDns] = ril_dns;
}
std::string CuttlefishConfig::ril_dns() const {
  return (*dictionary_)[kRilDns].asString();
}

static constexpr char kKgdb[] = "kgdb";
void CuttlefishConfig::set_kgdb(bool kgdb) {
  (*dictionary_)[kKgdb] = kgdb;
}
bool CuttlefishConfig::kgdb() const {
  return (*dictionary_)[kKgdb].asBool();
}

static constexpr char kEnableMinimalMode[] = "enable_minimal_mode";
bool CuttlefishConfig::enable_minimal_mode() const {
  return (*dictionary_)[kEnableMinimalMode].asBool();
}
void CuttlefishConfig::set_enable_minimal_mode(bool enable_minimal_mode) {
  (*dictionary_)[kEnableMinimalMode] = enable_minimal_mode;
}

static constexpr char kConsole[] = "console";
void CuttlefishConfig::set_console(bool console) {
  (*dictionary_)[kConsole] = console;
}
bool CuttlefishConfig::console() const {
  return (*dictionary_)[kConsole].asBool();
}
std::string CuttlefishConfig::console_dev() const {
  auto can_use_virtio_console = !kgdb() && !use_bootloader();
  std::string console_dev;
  if (can_use_virtio_console) {
    // If kgdb and the bootloader are disabled, the Android serial console
    // spawns on a virtio-console port. If the bootloader is enabled, virtio
    // console can't be used since uboot doesn't support it.
    console_dev = "hvc1";
  } else {
    // crosvm ARM does not support ttyAMA. ttyAMA is a part of ARM arch.
    Arch target = target_arch();
    if ((target == Arch::Arm64 || target == Arch::Arm) &&
        vm_manager() != vm_manager::CrosvmManager::name()) {
      console_dev = "ttyAMA0";
    } else {
      console_dev = "ttyS0";
    }
  }
  return console_dev;
}

static constexpr char kVhostNet[] = "vhost_net";
void CuttlefishConfig::set_vhost_net(bool vhost_net) {
  (*dictionary_)[kVhostNet] = vhost_net;
}
bool CuttlefishConfig::vhost_net() const {
  return (*dictionary_)[kVhostNet].asBool();
}

static constexpr char kEthernet[] = "ethernet";
void CuttlefishConfig::set_ethernet(bool ethernet) {
  (*dictionary_)[kEthernet] = ethernet;
}
bool CuttlefishConfig::ethernet() const {
  return (*dictionary_)[kEthernet].asBool();
}

static constexpr char kRecordScreen[] = "record_screen";
void CuttlefishConfig::set_record_screen(bool record_screen) {
  (*dictionary_)[kRecordScreen] = record_screen;
}
bool CuttlefishConfig::record_screen() const {
  return (*dictionary_)[kRecordScreen].asBool();
}

static constexpr char kSmt[] = "smt";
void CuttlefishConfig::set_smt(bool smt) {
  (*dictionary_)[kSmt] = smt;
}
bool CuttlefishConfig::smt() const {
  return (*dictionary_)[kSmt].asBool();
}

static constexpr char kEnableAudio[] = "enable_audio";
void CuttlefishConfig::set_enable_audio(bool enable) {
  (*dictionary_)[kEnableAudio] = enable;
}
bool CuttlefishConfig::enable_audio() const {
  return (*dictionary_)[kEnableAudio].asBool();
}

static constexpr char kProtectedVm[] = "protected_vm";
void CuttlefishConfig::set_protected_vm(bool protected_vm) {
  (*dictionary_)[kProtectedVm] = protected_vm;
}
bool CuttlefishConfig::protected_vm() const {
  return (*dictionary_)[kProtectedVm].asBool();
}

static constexpr char kTargetArch[] = "target_arch";
void CuttlefishConfig::set_target_arch(Arch target_arch) {
  (*dictionary_)[kTargetArch] = static_cast<int>(target_arch);
}
Arch CuttlefishConfig::target_arch() const {
  return static_cast<Arch>((*dictionary_)[kTargetArch].asInt());
}

static constexpr char kBootconfigSupported[] = "bootconfig_supported";
bool CuttlefishConfig::bootconfig_supported() const {
  return (*dictionary_)[kBootconfigSupported].asBool();
}
void CuttlefishConfig::set_bootconfig_supported(bool bootconfig_supported) {
  (*dictionary_)[kBootconfigSupported] = bootconfig_supported;
}

// Creates the (initially empty) config object and populates it with values from
// the config file if the CUTTLEFISH_CONFIG_FILE env variable is present.
// Returns nullptr if there was an error loading from file
/*static*/ CuttlefishConfig* CuttlefishConfig::BuildConfigImpl() {
  auto config_file_path = StringFromEnv(kCuttlefishConfigEnvVarName,
                                        GetGlobalConfigFileLink());
  auto ret = new CuttlefishConfig();
  if (ret) {
    auto loaded = ret->LoadFromFile(config_file_path.c_str());
    if (!loaded) {
      delete ret;
      return nullptr;
    }
  }
  return ret;
}

/*static*/ const CuttlefishConfig* CuttlefishConfig::Get() {
  static std::shared_ptr<CuttlefishConfig> config(BuildConfigImpl());
  return config.get();
}

/*static*/ bool CuttlefishConfig::ConfigExists() {
  auto config_file_path = StringFromEnv(kCuttlefishConfigEnvVarName,
                                        GetGlobalConfigFileLink());
  auto real_file_path = AbsolutePath(config_file_path.c_str());
  return FileExists(real_file_path);
}

CuttlefishConfig::CuttlefishConfig() : dictionary_(new Json::Value()) {}
// Can't use '= default' on the header because the compiler complains of
// Json::Value being an incomplete type
CuttlefishConfig::~CuttlefishConfig() = default;

CuttlefishConfig::CuttlefishConfig(CuttlefishConfig&&) = default;
CuttlefishConfig& CuttlefishConfig::operator=(CuttlefishConfig&&) = default;

bool CuttlefishConfig::LoadFromFile(const char* file) {
  auto real_file_path = AbsolutePath(file);
  if (real_file_path.empty()) {
    LOG(ERROR) << "Could not get real path for file " << file;
    return false;
  }
  Json::CharReaderBuilder builder;
  std::ifstream ifs(real_file_path);
  std::string errorMessage;
  if (!Json::parseFromStream(builder, ifs, dictionary_.get(), &errorMessage)) {
    LOG(ERROR) << "Could not read config file " << file << ": " << errorMessage;
    return false;
  }
  return true;
}
bool CuttlefishConfig::SaveToFile(const std::string& file) const {
  std::ofstream ofs(file);
  if (!ofs.is_open()) {
    LOG(ERROR) << "Unable to write to file " << file;
    return false;
  }
  ofs << *dictionary_;
  return !ofs.fail();
}

std::string CuttlefishConfig::AssemblyPath(
    const std::string& file_name) const {
  return AbsolutePath(assembly_dir() + "/" + file_name);
}

CuttlefishConfig::MutableInstanceSpecific CuttlefishConfig::ForInstance(int num) {
  return MutableInstanceSpecific(this, std::to_string(num));
}

CuttlefishConfig::InstanceSpecific CuttlefishConfig::ForInstance(int num) const {
  return InstanceSpecific(this, std::to_string(num));
}

CuttlefishConfig::InstanceSpecific CuttlefishConfig::ForDefaultInstance() const {
  return InstanceSpecific(this, std::to_string(GetInstance()));
}

std::vector<CuttlefishConfig::InstanceSpecific> CuttlefishConfig::Instances() const {
  const auto& json = (*dictionary_)[kInstances];
  std::vector<CuttlefishConfig::InstanceSpecific> instances;
  for (const auto& name : json.getMemberNames()) {
    instances.push_back(CuttlefishConfig::InstanceSpecific(this, name));
  }
  return instances;
}

int GetInstance() {
  static int instance_id = InstanceFromEnvironment();
  return instance_id;
}

int GetDefaultVsockCid() {
  // we assume that this function is used to configure CuttlefishConfig once
  static const int default_vsock_cid = 3 + GetInstance() - 1;
  return default_vsock_cid;
}

int GetVsockServerPort(const int base,
                       const int vsock_guest_cid /**< per instance guest cid */) {
    return base + (vsock_guest_cid - 3);
}

std::string GetGlobalConfigFileLink() {
  return StringFromEnv("HOME", ".") + "/.cuttlefish_config.json";
}

std::string ForCurrentInstance(const char* prefix) {
  std::ostringstream stream;
  stream << prefix << std::setfill('0') << std::setw(2) << GetInstance();
  return stream.str();
}
int ForCurrentInstance(int base) { return base + GetInstance() - 1; }

std::string RandomSerialNumber(const std::string& prefix) {
  const char hex_characters[] = "0123456789ABCDEF";
  std::srand(time(0));
  char str[10];
  for(int i=0; i<10; i++){
    str[i] = hex_characters[rand() % strlen(hex_characters)];
  }
  return prefix + str;
}

std::string DefaultHostArtifactsPath(const std::string& file_name) {
  return (StringFromEnv("ANDROID_SOONG_HOST_OUT", StringFromEnv("HOME", ".")) + "/") +
         file_name;
}

std::string HostBinaryPath(const std::string& binary_name) {
#ifdef __ANDROID__
  return binary_name;
#else
  return DefaultHostArtifactsPath("bin/" + binary_name);
#endif
}

std::string DefaultGuestImagePath(const std::string& file_name) {
  return (StringFromEnv("ANDROID_PRODUCT_OUT", StringFromEnv("HOME", "."))) +
         file_name;
}

bool HostSupportsQemuCli() {
  static bool supported =
      std::system(
          "/usr/lib/cuttlefish-common/bin/capability_query.py qemu_cli") == 0;
  return supported;
}

}  // namespace cuttlefish
