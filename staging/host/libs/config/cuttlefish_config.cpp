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
#include "host/libs/vm_manager/qemu_manager.h"

namespace cuttlefish {
namespace {

int InstanceFromEnvironment() {
  static constexpr char kInstanceEnvironmentVariable[] = "CUTTLEFISH_INSTANCE";
  static constexpr int kDefaultInstance = 1;

  // CUTTLEFISH_INSTANCE environment variable
  const char* instance_str = std::getenv(kInstanceEnvironmentVariable);
  if (!instance_str) {
    // Try to get it from the user instead
    instance_str = std::getenv("USER");

    if (!instance_str || std::strncmp(instance_str, kVsocUserPrefix,
                                      sizeof(kVsocUserPrefix) - 1)) {
      // No user or we don't recognize this user
      LOG(DEBUG) << "No user or non-vsoc user, returning default config";
      return kDefaultInstance;
    }
    instance_str += sizeof(kVsocUserPrefix) - 1;
  }

  int instance = std::atoi(instance_str);
  if (instance <= 0) {
    instance = kDefaultInstance;
  }

  return instance;
}

const char* kInstances = "instances";
const char* kAssemblyDir = "assembly_dir";
const char* kVmManager = "vm_manager";
const char* const kGpuMode = "gpu_mode";

const char* kCpus = "cpus";
const char* kMemoryMb = "memory_mb";
const char* kDpi = "dpi";
const char* kXRes = "x_res";
const char* kYRes = "y_res";
const char* kRefreshRateHz = "refresh_rate_hz";
const char* kDisplayConfigs = "display_configs";

const char* kKernelImagePath = "kernel_image_path";
const char* kUseUnpackedKernel = "use_unpacked_kernel";
const char* kDecompressedKernelImagePath = "decompressed_kernel_image_path";
const char* kDecompressKernel = "decompress_kernel";
const char* kGdbFlag = "gdb_flag";
const char* kRamdiskImagePath = "ramdisk_image_path";
const char* kInitramfsPath = "initramfs_path";
const char* kFinalRamdiskPath = "final_ramdisk_path";
const char* kVendorRamdiskImagePath = "vendor_ramdisk_image_path";

const char* kDeprecatedBootCompleted = "deprecated_boot_completed";

const char* kCuttlefishEnvPath = "cuttlefish_env_path";

const char* kAdbMode = "adb_mode";
const char* kSetupWizardMode = "setupwizard_mode";
const char* kTpmDevice = "tpm_device";

const char* kQemuBinary = "qemu_binary";
const char* kCrosvmBinary = "crosvm_binary";

const char* kEnableVncServer = "enable_vnc_server";

const char* kEnableSandbox = "enable_sandbox";
const char* kSeccompPolicyDir = "seccomp_policy_dir";

const char* kEnableGnssGrpcProxy = "enable_gnss_grpc_proxy";

const char* kEnableWebRTC = "enable_webrtc";
const char* kWebRTCAssetsDir = "webrtc_assets_dir";
const char* kWebRTCEnableADBWebSocket = "webrtc_enable_adb_websocket";

const char* kEnableVehicleHalServer = "enable_vehicle_hal_server";
const char* kVehicleHalServerBinary = "vehicle_hal_server_binary";

const char* kCustomActions = "custom_actions";

const char* kRestartSubprocesses = "restart_subprocesses";
const char* kRunAdbConnector = "run_adb_connector";

const char* kRunAsDaemon = "run_as_daemon";

const char* kDataPolicy = "data_policy";
const char* kBlankDataImageMb = "blank_data_image_mb";
const char* kBlankDataImageFmt = "blank_data_image_fmt";

const char* kWebRTCCertsDir = "webrtc_certs_dir";
const char* kSigServerPort = "webrtc_sig_server_port";
const char* kSigServerAddress = "webrtc_sig_server_addr";
const char* kSigServerPath = "webrtc_sig_server_path";
const char* kSigServerStrict = "webrtc_sig_server_strict";
const char* kWebrtcUdpPortRange = "webrtc_udp_port_range";
const char* kWebrtcTcpPortRange = "webrtc_tcp_port_range";
const char* kSigServerHeadersPath = "webrtc_sig_server_headers_path";

const char* kBootloader = "bootloader";
const char* kUseBootloader = "use_bootloader";

const char* kBootSlot = "boot_slot";

const char* kEnableMetrics = "enable_metrics";
const char* kMetricsBinary = "metrics_binary";

const char* kGuestEnforceSecurity = "guest_enforce_security";
const char* kGuestAuditSecurity = "guest_audit_security";
const char* kGuestForceNormalBoot = "guest_force_normal_boot";
const char* kBootImageKernelCmdline = "boot_image_kernel_cmdline";
const char* kExtraKernelCmdline = "extra_kernel_cmdline";

// modem simulator related
const char* kRunModemSimulator = "enable_modem_simulator";
const char* kModemSimulatorInstanceNumber = "modem_simulator_instance_number";
const char* kModemSimulatorSimType = "modem_simulator_sim_type";

const char* kRilDns = "ril_dns";

const char* kKgdb = "kgdb";

const char* kEnableMinimalMode = "enable_minimal_mode";

const char* kConsole = "console";

const char* kHostToolsVersion = "host_tools_version";

const char* kVhostNet = "vhost_net";
const char* kRecordScreen = "record_screen";

const char* kEthernet = "ethernet";

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

std::string CuttlefishConfig::assembly_dir() const {
  return (*dictionary_)[kAssemblyDir].asString();
}
void CuttlefishConfig::set_assembly_dir(const std::string& assembly_dir) {
  (*dictionary_)[kAssemblyDir] = assembly_dir;
}

std::string CuttlefishConfig::vm_manager() const {
  return (*dictionary_)[kVmManager].asString();
}
void CuttlefishConfig::set_vm_manager(const std::string& name) {
  (*dictionary_)[kVmManager] = name;
}

std::string CuttlefishConfig::gpu_mode() const {
  return (*dictionary_)[kGpuMode].asString();
}
void CuttlefishConfig::set_gpu_mode(const std::string& name) {
  (*dictionary_)[kGpuMode] = name;
}

int CuttlefishConfig::cpus() const { return (*dictionary_)[kCpus].asInt(); }
void CuttlefishConfig::set_cpus(int cpus) { (*dictionary_)[kCpus] = cpus; }

int CuttlefishConfig::memory_mb() const {
  return (*dictionary_)[kMemoryMb].asInt();
}
void CuttlefishConfig::set_memory_mb(int memory_mb) {
  (*dictionary_)[kMemoryMb] = memory_mb;
}

int CuttlefishConfig::dpi() const { return (*dictionary_)[kDpi].asInt(); }
void CuttlefishConfig::set_dpi(int dpi) { (*dictionary_)[kDpi] = dpi; }

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

int CuttlefishConfig::refresh_rate_hz() const {
  return (*dictionary_)[kRefreshRateHz].asInt();
}
void CuttlefishConfig::set_refresh_rate_hz(int refresh_rate_hz) {
  (*dictionary_)[kRefreshRateHz] = refresh_rate_hz;
}

std::string CuttlefishConfig::kernel_image_path() const {
  return (*dictionary_)[kKernelImagePath].asString();
}

void CuttlefishConfig::SetPath(const std::string& key,
                               const std::string& path) {
  if (!path.empty()) {
    (*dictionary_)[key] = AbsolutePath(path);
  }
}

void CuttlefishConfig::set_kernel_image_path(
    const std::string& kernel_image_path) {
  SetPath(kKernelImagePath, kernel_image_path);
}

bool CuttlefishConfig::use_unpacked_kernel() const {
  return (*dictionary_)[kUseUnpackedKernel].asBool();
}

void CuttlefishConfig::set_use_unpacked_kernel(bool use_unpacked_kernel) {
  (*dictionary_)[kUseUnpackedKernel] = use_unpacked_kernel;
}

bool CuttlefishConfig::decompress_kernel() const {
  return (*dictionary_)[kDecompressKernel].asBool();
}
void CuttlefishConfig::set_decompress_kernel(bool decompress_kernel) {
  (*dictionary_)[kDecompressKernel] = decompress_kernel;
}

std::string CuttlefishConfig::decompressed_kernel_image_path() const {
  return (*dictionary_)[kDecompressedKernelImagePath].asString();
}
void CuttlefishConfig::set_decompressed_kernel_image_path(
    const std::string& path) {
  SetPath(kDecompressedKernelImagePath, path);
}

std::string CuttlefishConfig::gdb_flag() const {
  return (*dictionary_)[kGdbFlag].asString();
}

void CuttlefishConfig::set_gdb_flag(const std::string& device) {
  (*dictionary_)[kGdbFlag] = device;
}

std::string CuttlefishConfig::ramdisk_image_path() const {
  return (*dictionary_)[kRamdiskImagePath].asString();
}
void CuttlefishConfig::set_ramdisk_image_path(
    const std::string& ramdisk_image_path) {
  SetPath(kRamdiskImagePath, ramdisk_image_path);
}

std::string CuttlefishConfig::initramfs_path() const {
  return (*dictionary_)[kInitramfsPath].asString();
}
void CuttlefishConfig::set_initramfs_path(const std::string& initramfs_path) {
  SetPath(kInitramfsPath, initramfs_path);
}

std::string CuttlefishConfig::final_ramdisk_path() const {
  return (*dictionary_)[kFinalRamdiskPath].asString();
}
void CuttlefishConfig::set_final_ramdisk_path(
    const std::string& final_ramdisk_path) {
  SetPath(kFinalRamdiskPath, final_ramdisk_path);
}

std::string CuttlefishConfig::vendor_ramdisk_image_path() const {
  return (*dictionary_)[kVendorRamdiskImagePath].asString();
}
void CuttlefishConfig::set_vendor_ramdisk_image_path(
    const std::string& vendor_ramdisk_image_path) {
  SetPath(kVendorRamdiskImagePath, vendor_ramdisk_image_path);
}

bool CuttlefishConfig::deprecated_boot_completed() const {
  return (*dictionary_)[kDeprecatedBootCompleted].asBool();
}
void CuttlefishConfig::set_deprecated_boot_completed(
    bool deprecated_boot_completed) {
  (*dictionary_)[kDeprecatedBootCompleted] = deprecated_boot_completed;
}

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

std::string CuttlefishConfig::setupwizard_mode() const {
  return (*dictionary_)[kSetupWizardMode].asString();
}

void CuttlefishConfig::set_setupwizard_mode(const std::string& mode) {
  (*dictionary_)[kSetupWizardMode] = mode;
}

std::string CuttlefishConfig::qemu_binary() const {
  return (*dictionary_)[kQemuBinary].asString();
}

void CuttlefishConfig::set_qemu_binary(const std::string& qemu_binary) {
  (*dictionary_)[kQemuBinary] = qemu_binary;
}

std::string CuttlefishConfig::crosvm_binary() const {
  return (*dictionary_)[kCrosvmBinary].asString();
}

void CuttlefishConfig::set_crosvm_binary(const std::string& crosvm_binary) {
  (*dictionary_)[kCrosvmBinary] = crosvm_binary;
}

std::string CuttlefishConfig::tpm_device() const {
  return (*dictionary_)[kTpmDevice].asString();
}

void CuttlefishConfig::set_tpm_device(const std::string& tpm_device) {
  (*dictionary_)[kTpmDevice] = tpm_device;
}

void CuttlefishConfig::set_enable_gnss_grpc_proxy(const bool enable_gnss_grpc_proxy) {
  (*dictionary_)[kEnableGnssGrpcProxy] = enable_gnss_grpc_proxy;
}

bool CuttlefishConfig::enable_gnss_grpc_proxy() const {
  return (*dictionary_)[kEnableGnssGrpcProxy].asBool();
}

bool CuttlefishConfig::enable_vnc_server() const {
  return (*dictionary_)[kEnableVncServer].asBool();
}

void CuttlefishConfig::set_enable_vnc_server(bool enable_vnc_server) {
  (*dictionary_)[kEnableVncServer] = enable_vnc_server;
}

void CuttlefishConfig::set_enable_sandbox(const bool enable_sandbox) {
  (*dictionary_)[kEnableSandbox] = enable_sandbox;
}

bool CuttlefishConfig::enable_sandbox() const {
  return (*dictionary_)[kEnableSandbox].asBool();
}

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

void CuttlefishConfig::set_enable_webrtc(bool enable_webrtc) {
  (*dictionary_)[kEnableWebRTC] = enable_webrtc;
}

bool CuttlefishConfig::enable_webrtc() const {
  return (*dictionary_)[kEnableWebRTC].asBool();
}

void CuttlefishConfig::set_enable_vehicle_hal_grpc_server(bool enable_vehicle_hal_grpc_server) {
  (*dictionary_)[kEnableVehicleHalServer] = enable_vehicle_hal_grpc_server;
}

bool CuttlefishConfig::enable_vehicle_hal_grpc_server() const {
  return (*dictionary_)[kEnableVehicleHalServer].asBool();
}

void CuttlefishConfig::set_vehicle_hal_grpc_server_binary(const std::string& vehicle_hal_server_binary) {
  (*dictionary_)[kVehicleHalServerBinary] = vehicle_hal_server_binary;
}

std::string CuttlefishConfig::vehicle_hal_grpc_server_binary() const {
  return (*dictionary_)[kVehicleHalServerBinary].asString();
}

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

void CuttlefishConfig::set_webrtc_assets_dir(const std::string& webrtc_assets_dir) {
  (*dictionary_)[kWebRTCAssetsDir] = webrtc_assets_dir;
}

std::string CuttlefishConfig::webrtc_assets_dir() const {
  return (*dictionary_)[kWebRTCAssetsDir].asString();
}

void CuttlefishConfig::set_webrtc_enable_adb_websocket(bool enable) {
    (*dictionary_)[kWebRTCEnableADBWebSocket] = enable;
}

bool CuttlefishConfig::webrtc_enable_adb_websocket() const {
    return (*dictionary_)[kWebRTCEnableADBWebSocket].asBool();
}

bool CuttlefishConfig::restart_subprocesses() const {
  return (*dictionary_)[kRestartSubprocesses].asBool();
}

void CuttlefishConfig::set_restart_subprocesses(bool restart_subprocesses) {
  (*dictionary_)[kRestartSubprocesses] = restart_subprocesses;
}

bool CuttlefishConfig::run_adb_connector() const {
  return (*dictionary_)[kRunAdbConnector].asBool();
}

void CuttlefishConfig::set_run_adb_connector(bool run_adb_connector) {
  (*dictionary_)[kRunAdbConnector] = run_adb_connector;
}

bool CuttlefishConfig::run_as_daemon() const {
  return (*dictionary_)[kRunAsDaemon].asBool();
}

void CuttlefishConfig::set_run_as_daemon(bool run_as_daemon) {
  (*dictionary_)[kRunAsDaemon] = run_as_daemon;
}
std::string CuttlefishConfig::data_policy() const {
  return (*dictionary_)[kDataPolicy].asString();
}

void CuttlefishConfig::set_data_policy(const std::string& data_policy) {
  (*dictionary_)[kDataPolicy] = data_policy;
}

int CuttlefishConfig::blank_data_image_mb() const {
  return (*dictionary_)[kBlankDataImageMb].asInt();
}

void CuttlefishConfig::set_blank_data_image_mb(int blank_data_image_mb) {
  (*dictionary_)[kBlankDataImageMb] = blank_data_image_mb;
}

std::string CuttlefishConfig::blank_data_image_fmt() const {
  return (*dictionary_)[kBlankDataImageFmt].asString();
}

void CuttlefishConfig::set_blank_data_image_fmt(const std::string& blank_data_image_fmt) {
  (*dictionary_)[kBlankDataImageFmt] = blank_data_image_fmt;
}

bool CuttlefishConfig::use_bootloader() const {
  return (*dictionary_)[kUseBootloader].asBool();
}

void CuttlefishConfig::set_use_bootloader(bool use_bootloader) {
  (*dictionary_)[kUseBootloader] = use_bootloader;
}

std::string CuttlefishConfig::bootloader() const {
  return (*dictionary_)[kBootloader].asString();
}

void CuttlefishConfig::set_bootloader(const std::string& bootloader) {
  SetPath(kBootloader, bootloader);
}

void CuttlefishConfig::set_boot_slot(const std::string& boot_slot) {
  (*dictionary_)[kBootSlot] = boot_slot;
}

std::string CuttlefishConfig::boot_slot() const {
  return (*dictionary_)[kBootSlot].asString();
}

void CuttlefishConfig::set_webrtc_certs_dir(const std::string& certs_dir) {
  (*dictionary_)[kWebRTCCertsDir] = certs_dir;
}

std::string CuttlefishConfig::webrtc_certs_dir() const {
  return (*dictionary_)[kWebRTCCertsDir].asString();
}

void CuttlefishConfig::set_sig_server_port(int port) {
  (*dictionary_)[kSigServerPort] = port;
}

int CuttlefishConfig::sig_server_port() const {
  return (*dictionary_)[kSigServerPort].asInt();
}

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

void CuttlefishConfig::set_sig_server_address(const std::string& addr) {
  (*dictionary_)[kSigServerAddress] = addr;
}

std::string CuttlefishConfig::sig_server_address() const {
  return (*dictionary_)[kSigServerAddress].asString();
}

void CuttlefishConfig::set_sig_server_path(const std::string& path) {
  // Don't use SetPath here, it's a URL path not a file system path
  (*dictionary_)[kSigServerPath] = path;
}

std::string CuttlefishConfig::sig_server_path() const {
  return (*dictionary_)[kSigServerPath].asString();
}

void CuttlefishConfig::set_sig_server_strict(bool strict) {
  (*dictionary_)[kSigServerStrict] = strict;
}

bool CuttlefishConfig::sig_server_strict() const {
  return (*dictionary_)[kSigServerStrict].asBool();
}

void CuttlefishConfig::set_sig_server_headers_path(const std::string& path) {
  SetPath(kSigServerHeadersPath, path);
}

std::string CuttlefishConfig::sig_server_headers_path() const {
  return (*dictionary_)[kSigServerHeadersPath].asString();
}

bool CuttlefishConfig::enable_modem_simulator() const {
  return (*dictionary_)[kRunModemSimulator].asBool();
}

void CuttlefishConfig::set_enable_modem_simulator(bool enable_modem_simulator) {
  (*dictionary_)[kRunModemSimulator] = enable_modem_simulator;
}

void CuttlefishConfig::set_modem_simulator_instance_number(
    int instance_number) {
  (*dictionary_)[kModemSimulatorInstanceNumber] = instance_number;
}

int CuttlefishConfig::modem_simulator_instance_number() const {
  return (*dictionary_)[kModemSimulatorInstanceNumber].asInt();
}

void CuttlefishConfig::set_modem_simulator_sim_type(int sim_type) {
  (*dictionary_)[kModemSimulatorSimType] = sim_type;
}

int CuttlefishConfig::modem_simulator_sim_type() const {
  return (*dictionary_)[kModemSimulatorSimType].asInt();
}

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

void CuttlefishConfig::set_guest_enforce_security(bool guest_enforce_security) {
  (*dictionary_)[kGuestEnforceSecurity] = guest_enforce_security;
}
bool CuttlefishConfig::guest_enforce_security() const {
  return (*dictionary_)[kGuestEnforceSecurity].asBool();
}

void CuttlefishConfig::set_guest_audit_security(bool guest_audit_security) {
  (*dictionary_)[kGuestAuditSecurity] = guest_audit_security;
}
bool CuttlefishConfig::guest_audit_security() const {
  return (*dictionary_)[kGuestAuditSecurity].asBool();
}

void CuttlefishConfig::set_guest_force_normal_boot(bool guest_force_normal_boot) {
  (*dictionary_)[kGuestForceNormalBoot] = guest_force_normal_boot;
}
bool CuttlefishConfig::guest_force_normal_boot() const {
  return (*dictionary_)[kGuestForceNormalBoot].asBool();
}

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

void CuttlefishConfig::set_metrics_binary(const std::string& metrics_binary) {
  (*dictionary_)[kMetricsBinary] = metrics_binary;
}

std::string CuttlefishConfig::metrics_binary() const {
  return (*dictionary_)[kMetricsBinary].asString();
}

void CuttlefishConfig::set_boot_image_kernel_cmdline(std::string boot_image_kernel_cmdline) {
  Json::Value args_json_obj(Json::arrayValue);
  for (const auto& arg : android::base::Split(boot_image_kernel_cmdline, " ")) {
    args_json_obj.append(arg);
  }
  (*dictionary_)[kBootImageKernelCmdline] = args_json_obj;
}
std::vector<std::string> CuttlefishConfig::boot_image_kernel_cmdline() const {
  std::vector<std::string> cmdline;
  for (const Json::Value& arg : (*dictionary_)[kBootImageKernelCmdline]) {
    cmdline.push_back(arg.asString());
  }
  return cmdline;
}

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

void CuttlefishConfig::set_ril_dns(const std::string& ril_dns) {
  (*dictionary_)[kRilDns] = ril_dns;
}
std::string CuttlefishConfig::ril_dns() const {
  return (*dictionary_)[kRilDns].asString();
}

void CuttlefishConfig::set_kgdb(bool kgdb) {
  (*dictionary_)[kKgdb] = kgdb;
}
bool CuttlefishConfig::kgdb() const {
  return (*dictionary_)[kKgdb].asBool();
}

bool CuttlefishConfig::enable_minimal_mode() const {
  return (*dictionary_)[kEnableMinimalMode].asBool();
}

void CuttlefishConfig::set_enable_minimal_mode(bool enable_minimal_mode) {
  (*dictionary_)[kEnableMinimalMode] = enable_minimal_mode;
}

void CuttlefishConfig::set_console(bool console) {
  (*dictionary_)[kConsole] = console;
}
bool CuttlefishConfig::console() const {
  return (*dictionary_)[kConsole].asBool();
}

void CuttlefishConfig::set_vhost_net(bool vhost_net) {
  (*dictionary_)[kVhostNet] = vhost_net;
}
bool CuttlefishConfig::vhost_net() const {
  return (*dictionary_)[kVhostNet].asBool();
}

void CuttlefishConfig::set_ethernet(bool ethernet) {
  (*dictionary_)[kEthernet] = ethernet;
}
bool CuttlefishConfig::ethernet() const {
  return (*dictionary_)[kEthernet].asBool();
}

void CuttlefishConfig::set_record_screen(bool record_screen) {
  (*dictionary_)[kRecordScreen] = record_screen;
}
bool CuttlefishConfig::record_screen() const {
  return (*dictionary_)[kRecordScreen].asBool();
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
  Json::Reader reader;
  std::ifstream ifs(real_file_path);
  if (!reader.parse(ifs, *dictionary_)) {
    LOG(ERROR) << "Could not read config file " << file << ": "
               << reader.getFormattedErrorMessages();
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

int GetDefaultPerInstanceVsockCid() {
  constexpr int kFirstGuestCid = 3;
  return HostSupportsVsock() ? ForCurrentInstance(kFirstGuestCid) : 0;
}

std::string DefaultHostArtifactsPath(const std::string& file_name) {
  return (StringFromEnv("ANDROID_SOONG_HOST_OUT", StringFromEnv("HOME", ".")) + "/") +
         file_name;
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

bool HostSupportsVsock() {
  static bool supported =
      std::system(
          "/usr/lib/cuttlefish-common/bin/capability_query.py vsock") == 0;
  return supported;
}
}  // namespace cuttlefish
