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
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>

#include <android-base/strings.h>
#include <android-base/logging.h>
#include <json/json.h>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "host/libs/vm_manager/qemu_manager.h"


namespace {

int InstanceFromEnvironment() {
  static constexpr char kInstanceEnvironmentVariable[] = "CUTTLEFISH_INSTANCE";
  static constexpr int kDefaultInstance = 1;

  // CUTTLEFISH_INSTANCE environment variable
  const char* instance_str = std::getenv(kInstanceEnvironmentVariable);
  if (!instance_str) {
    // Try to get it from the user instead
    instance_str = std::getenv("USER");

    if (!instance_str || std::strncmp(instance_str, vsoc::kVsocUserPrefix,
                                      sizeof(vsoc::kVsocUserPrefix) - 1)) {
      // No user or we don't recognize this user
      LOG(DEBUG) << "No user or non-vsoc user, returning default config";
      return kDefaultInstance;
    }
    instance_str += sizeof(vsoc::kVsocUserPrefix) - 1;

    // Set the environment variable so that child processes see it
    setenv(kInstanceEnvironmentVariable, instance_str, 0);
  }

  int instance = std::atoi(instance_str);
  if (instance <= 0) {
    instance = kDefaultInstance;
  }

  return instance;
}

const char* kInstances = "instances";
const char* kAssemblyDir = "assembly_dir";
const char* kSerialNumber = "serial_number";
const char* kInstanceDir = "instance_dir";
const char* kVmManager = "vm_manager";
const char* const kGpuMode = "gpu_mode";
const char* const kWaylandSocket = "wayland_socket";
const char* const kXDisplay = "x_display";
const char* kDeviceTitle = "device_title";

const char* kCpus = "cpus";
const char* kMemoryMb = "memory_mb";
const char* kDpi = "dpi";
const char* kXRes = "x_res";
const char* kYRes = "y_res";
const char* kRefreshRateHz = "refresh_rate_hz";

const char* kKernelImagePath = "kernel_image_path";
const char* kUseUnpackedKernel = "use_unpacked_kernel";
const char* kDecompressedKernelImagePath = "decompressed_kernel_image_path";
const char* kDecompressKernel = "decompress_kernel";
const char* kGdbFlag = "gdb_flag";
const char* kRamdiskImagePath = "ramdisk_image_path";
const char* kInitramfsPath = "initramfs_path";
const char* kFinalRamdiskPath = "final_ramdisk_path";
const char* kVendorRamdiskImagePath = "vendor_ramdisk_image_path";

const char* kVirtualDiskPaths = "virtual_disk_paths";
const char* kDeprecatedBootCompleted = "deprecated_boot_completed";

const char* kMobileBridgeName = "mobile_bridge_name";
const char* kMobileTapName = "mobile_tap_name";
const char* kWifiTapName = "wifi_tap_name";
const char* kVsockGuestCid = "vsock_guest_cid";

const char* kUuid = "uuid";
const char* kCuttlefishEnvPath = "cuttlefish_env_path";

const char* kAdbMode = "adb_mode";
const char* kHostPort = "host_port";
const char* kTpmPort = "tpm_port";
const char* kAdbIPAndPort = "adb_ip_and_port";
const char* kSetupWizardMode = "setupwizard_mode";
const char* kTpmDevice = "tpm_device";

const char* kQemuBinary = "qemu_binary";
const char* kCrosvmBinary = "crosvm_binary";
const char* kTpmBinary = "tpm_binary";
const char* kConsoleForwarderBinary = "console_forwarder_binary";
const char* kKernelLogMonitorBinary = "kernel_log_monitor_binary";

const char* kEnableVncServer = "enable_vnc_server";
const char* kVncServerBinary = "vnc_server_binary";
const char* kVncServerPort = "vnc_server_port";

const char* kEnableSandbox = "enable_sandbox";
const char* kSeccompPolicyDir = "seccomp_policy_dir";

const char* kEnableWebRTC = "enable_webrtc";
const char* kWebRTCBinary = "webrtc_binary";
const char* kWebRTCAssetsDir = "webrtc_assets_dir";
const char* kWebRTCPublicIP = "webrtc_public_ip";
const char* kWebRTCEnableADBWebSocket = "webrtc_enable_adb_websocket";

const char* kRestartSubprocesses = "restart_subprocesses";
const char* kRunAdbConnector = "run_adb_connector";
const char* kAdbConnectorBinary = "adb_connector_binary";
const char* kSocketVsockProxyBinary = "socket_vsock_proxy_binary";

const char* kRunAsDaemon = "run_as_daemon";

const char* kDataPolicy = "data_policy";
const char* kBlankDataImageMb = "blank_data_image_mb";
const char* kBlankDataImageFmt = "blank_data_image_fmt";

const char* kLogcatMode = "logcat_mode";
const char* kLogcatPort = "logcat_port";
const char* kLogcatReceiverBinary = "logcat_receiver_binary";
const char* kConfigServerPort = "config_server_port";
const char* kConfigServerBinary = "config_server_binary";

const char* kRunTombstoneReceiver = "enable_tombstone_logger";
const char* kTombstoneReceiverBinary = "tombstone_receiver_binary";
const char* kTombstoneReceiverPort = "tombstone_receiver_port";

const char* kWebRTCCertsDir = "webrtc_certs_dir";
const char* kSigServerBinary = "webrtc_sig_server_binary";
const char* kSigServerPort = "webrtc_sig_server_port";
const char* kSigServerAddress = "webrtc_sig_server_addr";
const char* kSigServerPath = "webrtc_sig_server_path";
const char* kSigServerStrict = "webrtc_sig_server_strict";
const char* kWebrtcDeviceId = "webrtc_device_id";
const char* kStartSigServer = "webrtc_start_sig_server";

const char* kBootloader = "bootloader";
const char* kUseBootloader = "use_bootloader";

const char* kBootSlot = "boot_slot";

const char* kEnableMetrics = "enable_metrics";
const char* kMetricsBinary = "metrics_binary";

const char* kLoopMaxPart = "loop_max_part";
const char* kGuestEnforceSecurity = "guest_enforce_security";
const char* kGuestAuditSecurity = "guest_audit_security";
const char* kGuestForceNormalBoot = "guest_force_normal_boot";
const char* kBootImageKernelCmdline = "boot_image_kernel_cmdline";
const char* kExtraKernelCmdline = "extra_kernel_cmdline";

const char* kFramesServerPort = "frames_server_port";
const char* kTouchServerPort = "touch_server_port";
const char* kKeyboardServerPort = "keyboard_server_port";

const char* kRilDns = "ril_dns";
const char* kKeymasterVsockPort = "keymaster_vsock_port";
const char* kWifiMacAddress = "wifi_mac_address";
}  // namespace

namespace vsoc {

const char* const kGpuModeGuestSwiftshader = "guest_swiftshader";
const char* const kGpuModeDrmVirgl = "drm_virgl";
const char* const kGpuModeGfxStream = "gfxstream";

std::string DefaultEnvironmentPath(const char* environment_key,
                                   const char* default_value,
                                   const char* subpath) {
  return cvd::StringFromEnv(environment_key, default_value) + "/" + subpath;
}

Json::Value* CuttlefishConfig::MutableInstanceSpecific::Dictionary() {
  return &(*config_->dictionary_)[kInstances][id_];
}

const Json::Value* CuttlefishConfig::InstanceSpecific::Dictionary() const {
  return &(*config_->dictionary_)[kInstances][id_];
}

std::string CuttlefishConfig::assembly_dir() const {
  return (*dictionary_)[kAssemblyDir].asString();
}
void CuttlefishConfig::set_assembly_dir(const std::string& assembly_dir) {
  (*dictionary_)[kAssemblyDir] = assembly_dir;
}

std::string CuttlefishConfig::InstanceSpecific::instance_dir() const {
  return (*Dictionary())[kInstanceDir].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_instance_dir(
    const std::string& instance_dir) {
  (*Dictionary())[kInstanceDir] = instance_dir;
}

std::string CuttlefishConfig::InstanceSpecific::instance_internal_dir() const {
  return PerInstancePath(kInternalDirName);
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

std::string CuttlefishConfig::wayland_socket() const {
  // Don't use SetPath here: the path is already fully formed.
  return (*dictionary_)[kWaylandSocket].asString();
}
void CuttlefishConfig::set_wayland_socket(const std::string& path) {
  (*dictionary_)[kWaylandSocket] = path;
}

std::string CuttlefishConfig::x_display() const {
  return (*dictionary_)[kXDisplay].asString();
}
void CuttlefishConfig::set_x_display(const std::string& address) {
  (*dictionary_)[kXDisplay] = address;
}

std::string CuttlefishConfig::InstanceSpecific::serial_number() const {
  return (*Dictionary())[kSerialNumber].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_serial_number(
    const std::string& serial_number) {
  (*Dictionary())[kSerialNumber] = serial_number;
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

int CuttlefishConfig::x_res() const { return (*dictionary_)[kXRes].asInt(); }
void CuttlefishConfig::set_x_res(int x_res) { (*dictionary_)[kXRes] = x_res; }

int CuttlefishConfig::y_res() const { return (*dictionary_)[kYRes].asInt(); }
void CuttlefishConfig::set_y_res(int y_res) { (*dictionary_)[kYRes] = y_res; }

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
    (*dictionary_)[key] = cvd::AbsolutePath(path);
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

std::vector<std::string> CuttlefishConfig::InstanceSpecific::virtual_disk_paths() const {
  std::vector<std::string> virtual_disks;
  auto virtual_disks_json_obj = (*Dictionary())[kVirtualDiskPaths];
  for (const auto& disk : virtual_disks_json_obj) {
    virtual_disks.push_back(disk.asString());
  }
  return virtual_disks;
}
void CuttlefishConfig::MutableInstanceSpecific::set_virtual_disk_paths(
    const std::vector<std::string>& virtual_disk_paths) {
  Json::Value virtual_disks_json_obj(Json::arrayValue);
  for (const auto& arg : virtual_disk_paths) {
    virtual_disks_json_obj.append(arg);
  }
  (*Dictionary())[kVirtualDiskPaths] = virtual_disks_json_obj;
}

std::string CuttlefishConfig::InstanceSpecific::kernel_log_pipe_name() const {
  return cvd::AbsolutePath(PerInstanceInternalPath("kernel-log-pipe"));
}

std::string CuttlefishConfig::InstanceSpecific::console_pipe_name() const {
  return cvd::AbsolutePath(PerInstanceInternalPath("console-pipe"));
}

bool CuttlefishConfig::deprecated_boot_completed() const {
  return (*dictionary_)[kDeprecatedBootCompleted].asBool();
}
void CuttlefishConfig::set_deprecated_boot_completed(
    bool deprecated_boot_completed) {
  (*dictionary_)[kDeprecatedBootCompleted] = deprecated_boot_completed;
}

std::string CuttlefishConfig::InstanceSpecific::access_kregistry_path() const {
  return cvd::AbsolutePath(PerInstancePath("access-kregistry"));
}

std::string CuttlefishConfig::InstanceSpecific::console_path() const {
  return cvd::AbsolutePath(PerInstancePath("console"));
}

std::string CuttlefishConfig::InstanceSpecific::logcat_path() const {
  return cvd::AbsolutePath(PerInstancePath("logcat"));
}

std::string CuttlefishConfig::InstanceSpecific::launcher_monitor_socket_path()
    const {
  return cvd::AbsolutePath(PerInstancePath("launcher_monitor.sock"));
}

std::string CuttlefishConfig::InstanceSpecific::launcher_log_path() const {
  return cvd::AbsolutePath(PerInstancePath("launcher.log"));
}

std::string CuttlefishConfig::InstanceSpecific::sdcard_path() const {
  return cvd::AbsolutePath(PerInstancePath("sdcard.img"));
}

std::string CuttlefishConfig::InstanceSpecific::mobile_bridge_name() const {
  return (*Dictionary())[kMobileBridgeName].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_mobile_bridge_name(
    const std::string& mobile_bridge_name) {
  (*Dictionary())[kMobileBridgeName] = mobile_bridge_name;
}

std::string CuttlefishConfig::InstanceSpecific::mobile_tap_name() const {
  return (*Dictionary())[kMobileTapName].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_mobile_tap_name(
    const std::string& mobile_tap_name) {
  (*Dictionary())[kMobileTapName] = mobile_tap_name;
}

std::string CuttlefishConfig::InstanceSpecific::wifi_tap_name() const {
  return (*Dictionary())[kWifiTapName].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_wifi_tap_name(
    const std::string& wifi_tap_name) {
  (*Dictionary())[kWifiTapName] = wifi_tap_name;
}

int CuttlefishConfig::InstanceSpecific::vsock_guest_cid() const {
  return (*Dictionary())[kVsockGuestCid].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_vsock_guest_cid(
    int vsock_guest_cid) {
  (*Dictionary())[kVsockGuestCid] = vsock_guest_cid;
}

std::string CuttlefishConfig::InstanceSpecific::uuid() const {
  return (*Dictionary())[kUuid].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_uuid(const std::string& uuid) {
  (*Dictionary())[kUuid] = uuid;
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

int CuttlefishConfig::InstanceSpecific::host_port() const {
  return (*Dictionary())[kHostPort].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_host_port(int host_port) {
  (*Dictionary())[kHostPort] = host_port;
}

int CuttlefishConfig::InstanceSpecific::tpm_port() const {
  return (*Dictionary())[kTpmPort].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_tpm_port(int tpm_port) {
  (*Dictionary())[kTpmPort] = tpm_port;
}

std::string CuttlefishConfig::InstanceSpecific::adb_ip_and_port() const {
  return (*Dictionary())[kAdbIPAndPort].asString();
}

void CuttlefishConfig::MutableInstanceSpecific::set_adb_ip_and_port(
    const std::string& ip_port) {
  (*Dictionary())[kAdbIPAndPort] = ip_port;
}

std::string CuttlefishConfig::InstanceSpecific::adb_device_name() const {
  if (adb_ip_and_port() != "") {
    return adb_ip_and_port();
  }
  LOG(ERROR) << "no adb_mode found, returning bad device name";
  return "NO_ADB_MODE_SET_NO_VALID_DEVICE_NAME";
}

std::string CuttlefishConfig::InstanceSpecific::device_title() const {
  return (*Dictionary())[kDeviceTitle].asString();
}

void CuttlefishConfig::MutableInstanceSpecific::set_device_title(
    const std::string& title) {
  (*Dictionary())[kDeviceTitle] = title;
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

std::string CuttlefishConfig::tpm_binary() const {
  return (*dictionary_)[kTpmBinary].asString();
}

void CuttlefishConfig::set_tpm_binary(const std::string& tpm_binary) {
  (*dictionary_)[kTpmBinary] = tpm_binary;
}

std::string CuttlefishConfig::tpm_device() const {
  return (*dictionary_)[kTpmDevice].asString();
}

void CuttlefishConfig::set_tpm_device(const std::string& tpm_device) {
  (*dictionary_)[kTpmDevice] = tpm_device;
}

std::string CuttlefishConfig::console_forwarder_binary() const {
  return (*dictionary_)[kConsoleForwarderBinary].asString();
}

void CuttlefishConfig::set_console_forwarder_binary(
    const std::string& binary) {
  (*dictionary_)[kConsoleForwarderBinary] = binary;
}

std::string CuttlefishConfig::kernel_log_monitor_binary() const {
  return (*dictionary_)[kKernelLogMonitorBinary].asString();
}

void CuttlefishConfig::set_kernel_log_monitor_binary(
    const std::string& kernel_log_monitor_binary) {
  (*dictionary_)[kKernelLogMonitorBinary] = kernel_log_monitor_binary;
}

bool CuttlefishConfig::enable_vnc_server() const {
  return (*dictionary_)[kEnableVncServer].asBool();
}

void CuttlefishConfig::set_enable_vnc_server(bool enable_vnc_server) {
  (*dictionary_)[kEnableVncServer] = enable_vnc_server;
}

std::string CuttlefishConfig::vnc_server_binary() const {
  return (*dictionary_)[kVncServerBinary].asString();
}

void CuttlefishConfig::set_vnc_server_binary(
    const std::string& vnc_server_binary) {
  (*dictionary_)[kVncServerBinary] = vnc_server_binary;
}

int CuttlefishConfig::InstanceSpecific::vnc_server_port() const {
  return (*Dictionary())[kVncServerPort].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_vnc_server_port(int vnc_server_port) {
  (*Dictionary())[kVncServerPort] = vnc_server_port;
}

int CuttlefishConfig::InstanceSpecific::frames_server_port() const {
  return (*Dictionary())[kFramesServerPort].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_frames_server_port(int frames_server_port) {
  (*Dictionary())[kFramesServerPort] = frames_server_port;
}

int CuttlefishConfig::InstanceSpecific::touch_server_port() const {
  return (*Dictionary())[kTouchServerPort].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_touch_server_port(int touch_server_port) {
  (*Dictionary())[kTouchServerPort] = touch_server_port;
}

int CuttlefishConfig::InstanceSpecific::keyboard_server_port() const {
  return (*Dictionary())[kKeyboardServerPort].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_keyboard_server_port(int keyboard_server_port) {
  (*Dictionary())[kKeyboardServerPort] = keyboard_server_port;
}

int CuttlefishConfig::InstanceSpecific::keymaster_vsock_port() const {
  return (*Dictionary())[kKeymasterVsockPort].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_keymaster_vsock_port(int keymaster_vsock_port) {
  (*Dictionary())[kKeymasterVsockPort] = keymaster_vsock_port;
}

int CuttlefishConfig::InstanceSpecific::tombstone_receiver_port() const {
  return (*Dictionary())[kTombstoneReceiverPort].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_tombstone_receiver_port(int tombstone_receiver_port) {
  (*Dictionary())[kTombstoneReceiverPort] = tombstone_receiver_port;
}

int CuttlefishConfig::InstanceSpecific::logcat_port() const {
  return (*Dictionary())[kLogcatPort].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_logcat_port(int logcat_port) {
  (*Dictionary())[kLogcatPort] = logcat_port;
}

int CuttlefishConfig::InstanceSpecific::config_server_port() const {
  return (*Dictionary())[kConfigServerPort].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_config_server_port(int config_server_port) {
  (*Dictionary())[kConfigServerPort] = config_server_port;
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

void CuttlefishConfig::set_webrtc_binary(const std::string& webrtc_binary) {
  (*dictionary_)[kWebRTCBinary] = webrtc_binary;
}

std::string CuttlefishConfig::webrtc_binary() const {
  return (*dictionary_)[kWebRTCBinary].asString();
}

void CuttlefishConfig::set_webrtc_assets_dir(const std::string& webrtc_assets_dir) {
  (*dictionary_)[kWebRTCAssetsDir] = webrtc_assets_dir;
}

std::string CuttlefishConfig::webrtc_assets_dir() const {
  return (*dictionary_)[kWebRTCAssetsDir].asString();
}

void CuttlefishConfig::set_webrtc_public_ip(
        const std::string& webrtc_public_ip) {
  (*dictionary_)[kWebRTCPublicIP] = webrtc_public_ip;
}

std::string CuttlefishConfig::webrtc_public_ip() const {
  return (*dictionary_)[kWebRTCPublicIP].asString();
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

std::string CuttlefishConfig::adb_connector_binary() const {
  return (*dictionary_)[kAdbConnectorBinary].asString();
}

void CuttlefishConfig::set_adb_connector_binary(
    const std::string& adb_connector_binary) {
  (*dictionary_)[kAdbConnectorBinary] = adb_connector_binary;
}

std::string CuttlefishConfig::socket_vsock_proxy_binary() const {
  return (*dictionary_)[kSocketVsockProxyBinary].asString();
}

void CuttlefishConfig::set_socket_vsock_proxy_binary(
    const std::string& socket_vsock_proxy_binary) {
  (*dictionary_)[kSocketVsockProxyBinary] = socket_vsock_proxy_binary;
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


void CuttlefishConfig::set_logcat_mode(const std::string& mode) {
  (*dictionary_)[kLogcatMode] = mode;
}

std::string CuttlefishConfig::logcat_mode() const {
  return (*dictionary_)[kLogcatMode].asString();
}

void CuttlefishConfig::set_logcat_receiver_binary(const std::string& binary) {
  SetPath(kLogcatReceiverBinary, binary);
}

std::string CuttlefishConfig::logcat_receiver_binary() const {
  return (*dictionary_)[kLogcatReceiverBinary].asString();
}

void CuttlefishConfig::set_config_server_binary(const std::string& binary) {
  SetPath(kConfigServerBinary, binary);
}

std::string CuttlefishConfig::config_server_binary() const {
  return (*dictionary_)[kConfigServerBinary].asString();
}

bool CuttlefishConfig::enable_tombstone_receiver() const {
  return (*dictionary_)[kRunTombstoneReceiver].asBool();
}

void CuttlefishConfig::set_enable_tombstone_receiver(bool enable_tombstone_receiver) {
  (*dictionary_)[kRunTombstoneReceiver] = enable_tombstone_receiver;
}

std::string CuttlefishConfig::tombstone_receiver_binary() const {
  return (*dictionary_)[kTombstoneReceiverBinary].asString();
}

void CuttlefishConfig::set_tombstone_receiver_binary(const std::string& e2e_test_binary) {
  (*dictionary_)[kTombstoneReceiverBinary] = e2e_test_binary;
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

void CuttlefishConfig::set_sig_server_binary(const std::string& binary) {
  SetPath(kSigServerBinary, binary);
}

std::string CuttlefishConfig::sig_server_binary() const {
  return (*dictionary_)[kSigServerBinary].asString();
}

void CuttlefishConfig::set_sig_server_port(int port) {
  (*dictionary_)[kSigServerPort] = port;
}

int CuttlefishConfig::sig_server_port() const {
  return (*dictionary_)[kSigServerPort].asInt();
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

void CuttlefishConfig::MutableInstanceSpecific::set_webrtc_device_id(
    const std::string& id) {
  (*Dictionary())[kWebrtcDeviceId] = id;
}

std::string CuttlefishConfig::InstanceSpecific::webrtc_device_id() const {
  return (*Dictionary())[kWebrtcDeviceId].asString();
}

void CuttlefishConfig::MutableInstanceSpecific::set_start_webrtc_signaling_server(bool start) {
  (*Dictionary())[kStartSigServer] = start;
}

bool CuttlefishConfig::InstanceSpecific::start_webrtc_sig_server() const {
  return (*Dictionary())[kStartSigServer].asBool();
}

std::string CuttlefishConfig::InstanceSpecific::touch_socket_path() const {
  return PerInstanceInternalPath("touch.sock");
}

std::string CuttlefishConfig::InstanceSpecific::keyboard_socket_path() const {
  return PerInstanceInternalPath("keyboard.sock");
}

std::string CuttlefishConfig::InstanceSpecific::frames_socket_path() const {
  return PerInstanceInternalPath("frames.sock");
}

void CuttlefishConfig::set_loop_max_part(int loop_max_part) {
  (*dictionary_)[kLoopMaxPart] = loop_max_part;
}
int CuttlefishConfig::loop_max_part() const {
  return (*dictionary_)[kLoopMaxPart].asInt();
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

void CuttlefishConfig::MutableInstanceSpecific::set_wifi_mac_address(
    const std::array<unsigned char, 6>& mac_address) {
  Json::Value mac_address_obj(Json::arrayValue);
  for (const auto& num : mac_address) {
    mac_address_obj.append(num);
  }
  (*Dictionary())[kWifiMacAddress] = mac_address_obj;
}

std::array<unsigned char, 6> CuttlefishConfig::InstanceSpecific::wifi_mac_address() const {
  std::array<unsigned char, 6> mac_address{0, 0, 0, 0, 0, 0};
  auto mac_address_obj = (*Dictionary())[kWifiMacAddress];
  if (mac_address_obj.size() != 6) {
    LOG(ERROR) << kWifiMacAddress << " entry had wrong size";
    return {};
  }
  for (int i = 0; i < 6; i++) {
    mac_address[i] = mac_address_obj[i].asInt();
  }
  return mac_address;
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
std::string CuttlefishConfig::ril_dns()const {
  return (*dictionary_)[kRilDns].asString();
}

// Creates the (initially empty) config object and populates it with values from
// the config file if the CUTTLEFISH_CONFIG_FILE env variable is present.
// Returns nullptr if there was an error loading from file
/*static*/ CuttlefishConfig* CuttlefishConfig::BuildConfigImpl() {
  auto config_file_path = cvd::StringFromEnv(kCuttlefishConfigEnvVarName,
                                             vsoc::GetGlobalConfigFileLink());
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
  auto config_file_path = cvd::StringFromEnv(kCuttlefishConfigEnvVarName,
                                             vsoc::GetGlobalConfigFileLink());
  auto real_file_path = cvd::AbsolutePath(config_file_path.c_str());
  return cvd::FileExists(real_file_path);
}

CuttlefishConfig::CuttlefishConfig() : dictionary_(new Json::Value()) {}
// Can't use '= default' on the header because the compiler complains of
// Json::Value being an incomplete type
CuttlefishConfig::~CuttlefishConfig() = default;

CuttlefishConfig::CuttlefishConfig(CuttlefishConfig&&) = default;
CuttlefishConfig& CuttlefishConfig::operator=(CuttlefishConfig&&) = default;

bool CuttlefishConfig::LoadFromFile(const char* file) {
  auto real_file_path = cvd::AbsolutePath(file);
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
  return cvd::AbsolutePath(assembly_dir() + "/" + file_name);
}

std::string CuttlefishConfig::composite_disk_path() const {
  return AssemblyPath("composite.img");
}

std::string CuttlefishConfig::InstanceSpecific::PerInstancePath(
    const char* file_name) const {
  return (instance_dir() + "/") + file_name;
}

std::string CuttlefishConfig::InstanceSpecific::PerInstanceInternalPath(
    const char* file_name) const {
  if (file_name[0] == '\0') {
    // Don't append a / if file_name is empty.
    return PerInstancePath(kInternalDirName);
  }
  auto relative_path = (std::string(kInternalDirName) + "/") + file_name;
  return PerInstancePath(relative_path.c_str());
}

std::string CuttlefishConfig::InstanceSpecific::instance_name() const {
  return ForCurrentInstance("cvd-");
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

std::string GetGlobalConfigFileLink() {
  return cvd::StringFromEnv("HOME", ".") + "/.cuttlefish_config.json";
}

std::string ForCurrentInstance(const char* prefix) {
  std::ostringstream stream;
  stream << prefix << std::setfill('0') << std::setw(2) << GetInstance();
  return stream.str();
}
int ForCurrentInstance(int base) { return base + GetInstance() - 1; }

int GetDefaultPerInstanceVsockCid() {
  constexpr int kFirstGuestCid = 3;
  return vsoc::HostSupportsVsock() ? ForCurrentInstance(kFirstGuestCid) : 0;
}

std::string DefaultHostArtifactsPath(const std::string& file_name) {
  return (cvd::StringFromEnv("ANDROID_HOST_OUT",
                             cvd::StringFromEnv("HOME", ".")) +
          "/") +
         file_name;
}

std::string DefaultGuestImagePath(const std::string& file_name) {
  return (cvd::StringFromEnv("ANDROID_PRODUCT_OUT",
                             cvd::StringFromEnv("HOME", "."))) +
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
}  // namespace vsoc
