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

#include <glog/logging.h>
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
      LOG(WARNING) << "No user or non-vsoc user, returning default config";
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

const char* kSerialNumber = "serial_number";
const char* kInstanceDir = "instance_dir";
const char* kVmManager = "vm_manager";
const char* const kGpuMode = "gpu_mode";
const char* const kWaylandSocket = "wayland_socket";
const char* const kXDisplay = "x_display";
const char* kHardwareName = "hardware_name";
const char* kDeviceTitle = "device_title";

const char* kCpus = "cpus";
const char* kMemoryMb = "memory_mb";
const char* kDpi = "dpi";
const char* kXRes = "x_res";
const char* kYRes = "y_res";
const char* kNumScreenBuffers = "num_screen_buffers";
const char* kRefreshRateHz = "refresh_rate_hz";

const char* kKernelImagePath = "kernel_image_path";
const char* kUseUnpackedKernel = "use_unpacked_kernel";
const char* kDecompressedKernelImagePath = "decompressed_kernel_image_path";
const char* kDecompressKernel = "decompress_kernel";
const char* kGdbFlag = "gdb_flag";
const char* kKernelCmdline = "kernel_cmdline";
const char* kRamdiskImagePath = "ramdisk_image_path";

const char* kVirtualDiskPaths = "virtual_disk_paths";
const char* kUsbV1SocketName = "usb_v1_socket_name";
const char* kVhciPort = "vhci_port";
const char* kUsbIpSocketName = "usb_ip_socket_name";
const char* kKernelLogPipeName = "kernel_log_pipe_name";
const char* kDeprecatedBootCompleted = "deprecated_boot_completed";
const char* kConsolePath = "console_path";
const char* kLogcatPath = "logcat_path";
const char* kLauncherLogPath = "launcher_log_path";
const char* kLauncherMonitorPath = "launcher_monitor_socket";
const char* kDtbPath = "dtb_path";
const char* kGsiFstabPath = "gsi.fstab_path";

const char* kMempath = "mempath";
const char* kIvshmemQemuSocketPath = "ivshmem_qemu_socket_path";
const char* kIvshmemClientSocketPath = "ivshmem_client_socket_path";
const char* kIvshmemVectorCount = "ivshmem_vector_count";

const char* kMobileBridgeName = "mobile_bridge_name";
const char* kMobileTapName = "mobile_tap_name";
const char* kWifiTapName = "wifi_tap_name";
const char* kWifiGuestMacAddr = "wifi_guest_mac_addr";
const char* kWifiHostMacAddr = "wifi_host_mac_addr";
const char* kEntropySource = "entropy_source";
const char* kVsockGuestCid = "vsock_guest_cid";

const char* kUuid = "uuid";
const char* kCuttlefishEnvPath = "cuttlefish_env_path";

const char* kAdbMode = "adb_mode";
const char* kAdbIPAndPort = "adb_ip_and_port";
const char* kSetupWizardMode = "setupwizard_mode";

const char* kQemuBinary = "qemu_binary";
const char* kCrosvmBinary = "crosvm_binary";
const char* kConsoleForwarderBinary = "console_forwarder_binary";
const char* kIvServerBinary = "ivserver_binary";
const char* kKernelLogMonitorBinary = "kernel_log_monitor_binary";

const char* kEnableVncServer = "enable_vnc_server";
const char* kVncServerBinary = "vnc_server_binary";
const char* kVncServerPort = "vnc_server_port";

const char* kEnableStreamAudio = "enable_stream_audio";
const char* kStreamAudioBinary = "stream_audio_binary";
const char* kStreamAudioPort = "stream_audio_port";

const char* kRestartSubprocesses = "restart_subprocesses";
const char* kRunAdbConnector = "run_adb_connector";
const char* kAdbConnectorBinary = "adb_connector_binary";
const char* kVirtualUsbManagerBinary = "virtual_usb_manager_binary";
const char* kSocketForwardProxyBinary = "socket_forward_proxy_binary";
const char* kSocketVsockProxyBinary = "socket_vsock_proxy_binary";

const char* kRunAsDaemon = "run_as_daemon";
const char* kRunE2eTest = "run_e2e_test";
const char* kE2eTestBinary = "e2e_test_binary";

const char* kDataPolicy = "data_policy";
const char* kBlankDataImageMb = "blank_data_image_mb";
const char* kBlankDataImageFmt = "blank_data_image_fmt";

const char* kLogcatMode = "logcat_mode";
const char* kLogcatVsockPort = "logcat_vsock_port";
const char* kConfigServerPort = "config_server_port";
const char* kFramesVsockPort = "frames_vsock_port";
const char* kLogcatReceiverBinary = "logcat_receiver_binary";
const char* kConfigServerBinary = "config_server_binary";

const char* kRunTombstoneReceiver = "enable_tombstone_logger";
const char* kTombstoneReceiverPort = "tombstone_logger_port";
const char* kTombstoneReceiverBinary = "tombstone_receiver_binary";
}  // namespace

namespace vsoc {

const char* const kGpuModeGuestSwiftshader = "guest_swiftshader";
const char* const kGpuModeDrmVirgl = "drm_virgl";

std::string DefaultEnvironmentPath(const char* environment_key,
                                   const char* default_value,
                                   const char* subpath) {
  return cvd::StringFromEnv(environment_key, default_value) + "/" + subpath;
}


std::string CuttlefishConfig::instance_dir() const {
  return (*dictionary_)[kInstanceDir].asString();
}
void CuttlefishConfig::set_instance_dir(const std::string& instance_dir) {
  (*dictionary_)[kInstanceDir] = instance_dir;
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

std::string CuttlefishConfig::hardware_name() const {
  return (*dictionary_)[kHardwareName].asString();
}
void CuttlefishConfig::set_hardware_name(const std::string& name) {
  (*dictionary_)[kHardwareName] = name;
}

std::string CuttlefishConfig::serial_number() const {
  return (*dictionary_)[kSerialNumber].asString();
}
void CuttlefishConfig::set_serial_number(const std::string& serial_number) {
  (*dictionary_)[kSerialNumber] = serial_number;
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

int CuttlefishConfig::num_screen_buffers() const {
  return (*dictionary_)[kNumScreenBuffers].asInt();
}
void CuttlefishConfig::set_num_screen_buffers(int num_screen_buffers) {
  (*dictionary_)[kNumScreenBuffers] = num_screen_buffers;
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

std::set<std::string> CuttlefishConfig::kernel_cmdline() const {
  std::set<std::string> args_set;
  auto args_json_obj = (*dictionary_)[kKernelCmdline];
  std::transform(args_json_obj.begin(), args_json_obj.end(),
                 std::inserter(args_set, args_set.begin()),
                 [](const Json::Value& it) { return it.asString(); });
  return args_set;
}
void CuttlefishConfig::set_kernel_cmdline(
    const std::set<std::string>& kernel_cmdline) {
  Json::Value args_json_obj(Json::arrayValue);
  for (const auto& arg : kernel_cmdline) {
    args_json_obj.append(arg);
  }
  (*dictionary_)[kKernelCmdline] = args_json_obj;
}
void CuttlefishConfig::add_kernel_cmdline(
    const std::set<std::string>& extra_args) {
  std::set<std::string> cmdline = kernel_cmdline();
  for (const auto& arg : extra_args) {
    if (cmdline.count(arg)) {
      LOG(ERROR) << "Kernel argument " << arg << " is duplicated";
    }
    cmdline.insert(arg);
  }
  set_kernel_cmdline(cmdline);
}
void CuttlefishConfig::add_kernel_cmdline(const std::string& kernel_cmdline) {
  std::stringstream args_stream(kernel_cmdline);
  std::set<std::string> kernel_cmdline_set;
  using is_iter = std::istream_iterator<std::string>;
  std::copy(is_iter(args_stream), is_iter(),
            std::inserter(kernel_cmdline_set, kernel_cmdline_set.begin()));
  add_kernel_cmdline(kernel_cmdline_set);
}
std::string CuttlefishConfig::kernel_cmdline_as_string() const {
  auto args_set = kernel_cmdline();
  std::stringstream output;
  std::copy(args_set.begin(), args_set.end(),
            std::ostream_iterator<std::string>(output, " "));
  return output.str();
}

std::string CuttlefishConfig::ramdisk_image_path() const {
  return (*dictionary_)[kRamdiskImagePath].asString();
}
void CuttlefishConfig::set_ramdisk_image_path(
    const std::string& ramdisk_image_path) {
  SetPath(kRamdiskImagePath, ramdisk_image_path);
}

std::vector<std::string> CuttlefishConfig::virtual_disk_paths() const {
  std::vector<std::string> virtual_disks;
  auto virtual_disks_json_obj = (*dictionary_)[kVirtualDiskPaths];
  for (const auto& disk : virtual_disks_json_obj) {
    virtual_disks.push_back(disk.asString());
  }
  return virtual_disks;
}
void CuttlefishConfig::set_virtual_disk_paths(
    const std::vector<std::string>& virtual_disk_paths) {
  Json::Value virtual_disks_json_obj(Json::arrayValue);
  for (const auto& arg : virtual_disk_paths) {
    virtual_disks_json_obj.append(arg);
  }
  (*dictionary_)[kVirtualDiskPaths] = virtual_disks_json_obj;
}

std::string CuttlefishConfig::dtb_path() const {
  return (*dictionary_)[kDtbPath].asString();
}
void CuttlefishConfig::set_dtb_path(const std::string& dtb_path) {
  SetPath(kDtbPath, dtb_path);
}

std::string CuttlefishConfig::gsi_fstab_path() const {
  return (*dictionary_)[kGsiFstabPath].asString();
}
void CuttlefishConfig::set_gsi_fstab_path(const std::string& path){
  SetPath(kGsiFstabPath, path);
}

std::string CuttlefishConfig::mempath() const {
  return (*dictionary_)[kMempath].asString();
}
void CuttlefishConfig::set_mempath(const std::string& mempath) {
  SetPath(kMempath, mempath);
}

std::string CuttlefishConfig::ivshmem_qemu_socket_path() const {
  return (*dictionary_)[kIvshmemQemuSocketPath].asString();
}
void CuttlefishConfig::set_ivshmem_qemu_socket_path(
    const std::string& ivshmem_qemu_socket_path) {
  SetPath(kIvshmemQemuSocketPath, ivshmem_qemu_socket_path);
}

std::string CuttlefishConfig::ivshmem_client_socket_path() const {
  return (*dictionary_)[kIvshmemClientSocketPath].asString();
}
void CuttlefishConfig::set_ivshmem_client_socket_path(
    const std::string& ivshmem_client_socket_path) {
  SetPath(kIvshmemClientSocketPath, ivshmem_client_socket_path);
}

int CuttlefishConfig::ivshmem_vector_count() const {
  return (*dictionary_)[kIvshmemVectorCount].asInt();
}
void CuttlefishConfig::set_ivshmem_vector_count(int ivshmem_vector_count) {
  (*dictionary_)[kIvshmemVectorCount] = ivshmem_vector_count;
}

std::string CuttlefishConfig::usb_v1_socket_name() const {
  return (*dictionary_)[kUsbV1SocketName].asString();
}
void CuttlefishConfig::set_usb_v1_socket_name(
    const std::string& usb_v1_socket_name) {
  (*dictionary_)[kUsbV1SocketName] = usb_v1_socket_name;
}

int CuttlefishConfig::vhci_port() const {
  return (*dictionary_)[kVhciPort].asInt();
}
void CuttlefishConfig::set_vhci_port(int vhci_port) {
  (*dictionary_)[kVhciPort] = vhci_port;
}

std::string CuttlefishConfig::usb_ip_socket_name() const {
  return (*dictionary_)[kUsbIpSocketName].asString();
}
void CuttlefishConfig::set_usb_ip_socket_name(
    const std::string& usb_ip_socket_name) {
  (*dictionary_)[kUsbIpSocketName] = usb_ip_socket_name;
}

std::string CuttlefishConfig::kernel_log_pipe_name() const {
  return (*dictionary_)[kKernelLogPipeName].asString();
}
void CuttlefishConfig::set_kernel_log_pipe_name(
    const std::string& kernel_log_pipe_name) {
  (*dictionary_)[kKernelLogPipeName] = kernel_log_pipe_name;
}

bool CuttlefishConfig::deprecated_boot_completed() const {
  return (*dictionary_)[kDeprecatedBootCompleted].asBool();
}
void CuttlefishConfig::set_deprecated_boot_completed(
    bool deprecated_boot_completed) {
  (*dictionary_)[kDeprecatedBootCompleted] = deprecated_boot_completed;
}

std::string CuttlefishConfig::console_path() const {
  return (*dictionary_)[kConsolePath].asString();
}
void CuttlefishConfig::set_console_path(const std::string& console_path) {
  SetPath(kConsolePath, console_path);
}

std::string CuttlefishConfig::logcat_path() const {
  return (*dictionary_)[kLogcatPath].asString();
}
void CuttlefishConfig::set_logcat_path(const std::string& logcat_path) {
  SetPath(kLogcatPath, logcat_path);
}

std::string CuttlefishConfig::launcher_monitor_socket_path() const {
  return (*dictionary_)[kLauncherMonitorPath].asString();
}
void CuttlefishConfig::set_launcher_monitor_socket_path(
    const std::string& launcher_monitor_path) {
  SetPath(kLauncherMonitorPath, launcher_monitor_path);
}

std::string CuttlefishConfig::launcher_log_path() const {
  return (*dictionary_)[kLauncherLogPath].asString();
}
void CuttlefishConfig::set_launcher_log_path(
    const std::string& launcher_log_path) {
  (*dictionary_)[kLauncherLogPath] = launcher_log_path;
}

std::string CuttlefishConfig::mobile_bridge_name() const {
  return (*dictionary_)[kMobileBridgeName].asString();
}
void CuttlefishConfig::set_mobile_bridge_name(
    const std::string& mobile_bridge_name) {
  (*dictionary_)[kMobileBridgeName] = mobile_bridge_name;
}

std::string CuttlefishConfig::wifi_guest_mac_addr() const {
  return (*dictionary_)[kWifiGuestMacAddr].asString();
}
void CuttlefishConfig::set_wifi_guest_mac_addr(
    const std::string& wifi_guest_mac_addr) {
  (*dictionary_)[kWifiGuestMacAddr] = wifi_guest_mac_addr;
}

std::string CuttlefishConfig::wifi_host_mac_addr() const {
  return (*dictionary_)[kWifiHostMacAddr].asString();
}
void CuttlefishConfig::set_wifi_host_mac_addr(
    const std::string& wifi_host_mac_addr) {
  (*dictionary_)[kWifiHostMacAddr] = wifi_host_mac_addr;
}

std::string CuttlefishConfig::mobile_tap_name() const {
  return (*dictionary_)[kMobileTapName].asString();
}
void CuttlefishConfig::set_mobile_tap_name(const std::string& mobile_tap_name) {
  (*dictionary_)[kMobileTapName] = mobile_tap_name;
}

std::string CuttlefishConfig::wifi_tap_name() const {
  return (*dictionary_)[kWifiTapName].asString();
}
void CuttlefishConfig::set_wifi_tap_name(const std::string& wifi_tap_name) {
  (*dictionary_)[kWifiTapName] = wifi_tap_name;
}

std::string CuttlefishConfig::entropy_source() const {
  return (*dictionary_)[kEntropySource].asString();
}
void CuttlefishConfig::set_entropy_source(const std::string& entropy_source) {
  (*dictionary_)[kEntropySource] = entropy_source;
}

int CuttlefishConfig::vsock_guest_cid() const {
  return (*dictionary_)[kVsockGuestCid].asInt();
}

void CuttlefishConfig::set_vsock_guest_cid(int vsock_guest_cid) {
  (*dictionary_)[kVsockGuestCid] = vsock_guest_cid;
}

std::string CuttlefishConfig::uuid() const {
  return (*dictionary_)[kUuid].asString();
}
void CuttlefishConfig::set_uuid(const std::string& uuid) {
  (*dictionary_)[kUuid] = uuid;
}

void CuttlefishConfig::set_cuttlefish_env_path(const std::string& path) {
  SetPath(kCuttlefishEnvPath, path);
}
std::string CuttlefishConfig::cuttlefish_env_path() const {
  return (*dictionary_)[kCuttlefishEnvPath].asString();
}

static AdbMode stringToAdbMode(std::string mode) {
  std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
  if (mode == "tunnel") {
    return AdbMode::Tunnel;
  } else if (mode == "vsock_tunnel") {
    return AdbMode::VsockTunnel;
  } else if (mode == "vsock_half_tunnel") {
    return AdbMode::VsockHalfTunnel;
  } else if (mode == "native_vsock") {
    return AdbMode::NativeVsock;
  } else if (mode == "usb") {
    return AdbMode::Usb;
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

std::string CuttlefishConfig::adb_ip_and_port() const {
  return (*dictionary_)[kAdbIPAndPort].asString();
}

void CuttlefishConfig::set_adb_ip_and_port(const std::string& ip_port) {
  (*dictionary_)[kAdbIPAndPort] = ip_port;
}

std::string CuttlefishConfig::adb_device_name() const {
  // TODO(schuffelen): Deal with duplication between here and launch.cc
  bool tunnelMode = adb_mode().count(AdbMode::Tunnel) > 0;
  bool vsockTunnel = adb_mode().count(AdbMode::VsockTunnel) > 0;
  bool vsockHalfProxy = adb_mode().count(AdbMode::VsockHalfTunnel) > 0;
  bool nativeVsock = adb_mode().count(AdbMode::NativeVsock) > 0;
  if (tunnelMode || vsockTunnel || vsockHalfProxy || nativeVsock) {
    return adb_ip_and_port();
  } else if (adb_mode().count(AdbMode::Usb) > 0) {
    return serial_number();
  }
  LOG(ERROR) << "no adb_mode found, returning bad device name";
  return "NO_ADB_MODE_SET_NO_VALID_DEVICE_NAME";
}

std::string CuttlefishConfig::device_title() const {
  return (*dictionary_)[kDeviceTitle].asString();
}

void CuttlefishConfig::set_device_title(const std::string& title) {
  (*dictionary_)[kDeviceTitle] = title;
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

std::string CuttlefishConfig::console_forwarder_binary() const {
  return (*dictionary_)[kConsoleForwarderBinary].asString();
}

void CuttlefishConfig::set_console_forwarder_binary(
    const std::string& binary) {
  (*dictionary_)[kConsoleForwarderBinary] = binary;
}

std::string CuttlefishConfig::ivserver_binary() const {
  return (*dictionary_)[kIvServerBinary].asString();
}

void CuttlefishConfig::set_ivserver_binary(const std::string& ivserver_binary) {
  (*dictionary_)[kIvServerBinary] = ivserver_binary;
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

int CuttlefishConfig::vnc_server_port() const {
  return (*dictionary_)[kVncServerPort].asInt();
}

void CuttlefishConfig::set_vnc_server_port(int vnc_server_port) {
  (*dictionary_)[kVncServerPort] = vnc_server_port;
}

bool CuttlefishConfig::enable_stream_audio() const {
  return (*dictionary_)[kEnableStreamAudio].asBool();
}

void CuttlefishConfig::set_enable_stream_audio(bool enable_stream_audio) {
  (*dictionary_)[kEnableStreamAudio] = enable_stream_audio;
}

std::string CuttlefishConfig::stream_audio_binary() const {
  return (*dictionary_)[kStreamAudioBinary].asString();
}

void CuttlefishConfig::set_stream_audio_binary(
    const std::string& stream_audio_binary) {
  (*dictionary_)[kStreamAudioBinary] = stream_audio_binary;
}

int CuttlefishConfig::stream_audio_port() const {
  return (*dictionary_)[kStreamAudioPort].asInt();
}

void CuttlefishConfig::set_stream_audio_port(int stream_audio_port) {
  (*dictionary_)[kStreamAudioPort] = stream_audio_port;
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

std::string CuttlefishConfig::virtual_usb_manager_binary() const {
  return (*dictionary_)[kVirtualUsbManagerBinary].asString();
}

void CuttlefishConfig::set_virtual_usb_manager_binary(
    const std::string& virtual_usb_manager_binary) {
  (*dictionary_)[kVirtualUsbManagerBinary] = virtual_usb_manager_binary;
}

std::string CuttlefishConfig::socket_forward_proxy_binary() const {
  return (*dictionary_)[kSocketForwardProxyBinary].asString();
}

void CuttlefishConfig::set_socket_forward_proxy_binary(
    const std::string& socket_forward_proxy_binary) {
  (*dictionary_)[kSocketForwardProxyBinary] = socket_forward_proxy_binary;
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

bool CuttlefishConfig::run_e2e_test() const {
  return (*dictionary_)[kRunE2eTest].asBool();
}

void CuttlefishConfig::set_run_e2e_test(bool run_e2e_test) {
  (*dictionary_)[kRunE2eTest] = run_e2e_test;
}

std::string CuttlefishConfig::e2e_test_binary() const {
  return (*dictionary_)[kE2eTestBinary].asString();
}

void CuttlefishConfig::set_e2e_test_binary(const std::string& e2e_test_binary) {
  (*dictionary_)[kE2eTestBinary] = e2e_test_binary;
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

void CuttlefishConfig::set_logcat_vsock_port(int port) {
  (*dictionary_)[kLogcatVsockPort] = port;
}

int CuttlefishConfig::logcat_vsock_port() const {
  return (*dictionary_)[kLogcatVsockPort].asInt();
}

void CuttlefishConfig::set_config_server_port(int port) {
  (*dictionary_)[kConfigServerPort] = port;
}

int CuttlefishConfig::config_server_port() const {
  return (*dictionary_)[kConfigServerPort].asInt();
}

void CuttlefishConfig::set_frames_vsock_port(int port) {
  (*dictionary_)[kFramesVsockPort] = port;
}

int CuttlefishConfig::frames_vsock_port() const {
  return (*dictionary_)[kFramesVsockPort].asInt();
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

void CuttlefishConfig::set_tombstone_receiver_port(int port) {
  (*dictionary_)[kTombstoneReceiverPort] = port;
}

int CuttlefishConfig::tombstone_receiver_port() const {
  return (*dictionary_)[kTombstoneReceiverPort].asInt();
}

bool CuttlefishConfig::enable_ivserver() const {
  return hardware_name() == "cutf_ivsh";
}

std::string CuttlefishConfig::touch_socket_path() const {
  return PerInstancePath("touch.sock");
}

std::string CuttlefishConfig::keyboard_socket_path() const {
  return PerInstancePath("keyboard.sock");
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

/*static*/ CuttlefishConfig* CuttlefishConfig::Get() {
  static std::shared_ptr<CuttlefishConfig> config(BuildConfigImpl());
  return config.get();
}

CuttlefishConfig::CuttlefishConfig() : dictionary_(new Json::Value()) {}
// Can't use '= default' on the header because the compiler complains of
// Json::Value being an incomplete type
CuttlefishConfig::~CuttlefishConfig() {}

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

std::string CuttlefishConfig::PerInstancePath(const char* file_name) const {
  return (instance_dir() + "/") + file_name;
}

std::string CuttlefishConfig::instance_name() const {
  return GetPerInstanceDefault("cvd-");
}

int GetInstance() {
  static int instance_id = InstanceFromEnvironment();
  return instance_id;
}

std::string GetGlobalConfigFileLink() {
  return cvd::StringFromEnv("HOME", ".") + "/.cuttlefish_config.json";
}

std::string GetDomain() {
  return CuttlefishConfig::Get()->ivshmem_client_socket_path();
}

std::string GetPerInstanceDefault(const char* prefix) {
  std::ostringstream stream;
  stream << prefix << std::setfill('0') << std::setw(2) << GetInstance();
  return stream.str();
}
int GetPerInstanceDefault(int base) { return base + GetInstance() - 1; }

std::string GetDefaultPerInstanceDir() {
  std::ostringstream stream;
  stream << std::getenv("HOME") << "/cuttlefish_runtime";
  return stream.str();
}

std::string GetDefaultMempath() {
  return GetPerInstanceDefault("/var/run/shm/cvd-");
}

int GetDefaultPerInstanceVsockCid() {
  constexpr int kFirstGuestCid = 3;
  return vsoc::HostSupportsVsock() ? GetPerInstanceDefault(kFirstGuestCid) : 0;
}

std::string DefaultHostArtifactsPath(const std::string& file_name) {
  return (cvd::StringFromEnv("ANDROID_HOST_OUT",
                             cvd::StringFromEnv("HOME", ".")) +
          "/") +
         file_name;
}

std::string DefaultGuestImagePath(const std::string& file_name) {
  return (cvd::StringFromEnv("ANDROID_PRODUCT_OUT",
                             cvd::StringFromEnv("HOME", ".")) +
          "/") +
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
