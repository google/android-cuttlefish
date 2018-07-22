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

#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <json/json.h>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"

DEFINE_string(config_file,
              vsoc::GetGlobalConfigFileLink(),
              "A file from where to load the config values. This flag is "
              "ignored by the launcher");

namespace {

int InstanceFromEnvironment() {
  static constexpr char kInstanceEnvironmentVariable[] = "CUTTLEFISH_INSTANCE";
  static constexpr char kVsocUserPrefix[] = "vsoc-";
  static constexpr int kDefaultInstance = 1;

  // CUTTLEFISH_INSTANCE environment variable
  const char* instance_str = std::getenv(kInstanceEnvironmentVariable);
  if (!instance_str) {
    // Try to get it from the user instead
    instance_str = std::getenv("USER");
    if (!instance_str || std::strncmp(instance_str, kVsocUserPrefix,
                                      sizeof(kVsocUserPrefix) - 1)) {
      // No user or we don't recognize this user
      return kDefaultInstance;
    }
    instance_str += sizeof(kVsocUserPrefix) - 1;
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

const char* kCpus = "cpus";
const char* kMemoryMb = "memory_mb";
const char* kDpi = "dpi";
const char* kXRes = "x_res";
const char* kYRes = "y_res";
const char* kRefreshRateHz = "refresh_rate_hz";

const char* kKernelImagePath = "kernel_image_path";
const char* kGdbFlag = "gdb_flag";
const char* kKernelArgs = "kernel_args";
const char* kRamdiskImagePath = "ramdisk_image_path";

const char* kSystemImagePath = "system_image_path";
const char* kCacheImagePath = "cache_image_path";
const char* kDataImagePath = "data_image_path";
const char* kVendorImagePath = "vendor_image_path";
const char* kUsbV1SocketName = "usb_v1_socket_name";
const char* kVhciPort = "vhci_port";
const char* kUsbIpSocketName = "usb_ip_socket_name";
const char* kKernelLogSocketName = "kernel_log_socket_name";
const char* kConsolePath = "console_path";
const char* kLogcatPath = "logcat_path";
const char* kLauncherLogPath = "launcher_log_path";
const char* kLauncherMonitorPath = "launcher_monitor_socket";
const char* kDtbPath = "dtb_path";

const char* kMempath = "mempath";
const char* kIvshmemQemuSocketPath = "ivshmem_qemu_socket_path";
const char* kIvshmemClientSocketPath = "ivshmem_client_socket_path";
const char* kIvshmemVectorCount = "ivshmem_vector_count";

const char* kMobileBridgeName = "mobile_bridge_name";
const char* kMobileTapName = "mobile_tap_name";
const char* kWifiBridgeName = "wifi_bridge_name";
const char* kWifiTapName = "wifi_tap_name";
const char* kWifiGuestMacAddr = "wifi_guest_mac_addr";
const char* kWifiHostMacAddr = "wifi_host_mac_addr";
const char* kEntropySource = "entropy_source";

const char* kUuid = "uuid";
const char* kDisableDacSecurity = "disable_dac_security";
const char* kDisableAppArmorSecurity = "disable_app_armor_security";
const char* kCuttlefishEnvPath = "cuttlefish_env_path";

const char* kAdbMode = "adb_mode";
}  // namespace

namespace vsoc {

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

std::string CuttlefishConfig::gdb_flag() const {
  return (*dictionary_)[kGdbFlag].asString();
}

void CuttlefishConfig::set_gdb_flag(
    const std::string& device) {
  SetPath(kGdbFlag, device);
}

std::string CuttlefishConfig::kernel_args() const {
  return (*dictionary_)[kKernelArgs].asString();
}
void CuttlefishConfig::set_kernel_args(const std::string& kernel_args) {
  (*dictionary_)[kKernelArgs] = kernel_args;
}

std::string CuttlefishConfig::ramdisk_image_path() const {
  return (*dictionary_)[kRamdiskImagePath].asString();
}
void CuttlefishConfig::set_ramdisk_image_path(
    const std::string& ramdisk_image_path) {
  SetPath(kRamdiskImagePath, ramdisk_image_path);
}

std::string CuttlefishConfig::system_image_path() const {
  return (*dictionary_)[kSystemImagePath].asString();
}
void CuttlefishConfig::set_system_image_path(
    const std::string& system_image_path) {
  SetPath(kSystemImagePath, system_image_path);
}

std::string CuttlefishConfig::cache_image_path() const {
  return (*dictionary_)[kCacheImagePath].asString();
}
void CuttlefishConfig::set_cache_image_path(
    const std::string& cache_image_path) {
  SetPath(kCacheImagePath, cache_image_path);
}

std::string CuttlefishConfig::data_image_path() const {
  return (*dictionary_)[kDataImagePath].asString();
}
void CuttlefishConfig::set_data_image_path(const std::string& data_image_path) {
  SetPath(kDataImagePath, data_image_path);
}

std::string CuttlefishConfig::vendor_image_path() const {
  return (*dictionary_)[kVendorImagePath].asString();
}
void CuttlefishConfig::set_vendor_image_path(
    const std::string& vendor_image_path) {
  SetPath(kVendorImagePath, vendor_image_path);
}

std::string CuttlefishConfig::dtb_path() const {
  return (*dictionary_)[kDtbPath].asString();
}
void CuttlefishConfig::set_dtb_path(const std::string& dtb_path) {
  SetPath(kDtbPath, dtb_path);
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

std::string CuttlefishConfig::kernel_log_socket_name() const {
  return (*dictionary_)[kKernelLogSocketName].asString();
}
void CuttlefishConfig::set_kernel_log_socket_name(
    const std::string& kernel_log_socket_name) {
  (*dictionary_)[kKernelLogSocketName] = kernel_log_socket_name;
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
    const std::string& launhcer_monitor_path) {
  (*dictionary_)[kLauncherMonitorPath] = launhcer_monitor_path;
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

std::string CuttlefishConfig::wifi_bridge_name() const {
  return (*dictionary_)[kWifiBridgeName].asString();
}
void CuttlefishConfig::set_wifi_bridge_name(
    const std::string& wifi_bridge_name) {
  (*dictionary_)[kWifiBridgeName] = wifi_bridge_name;
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

std::string CuttlefishConfig::uuid() const {
  return (*dictionary_)[kUuid].asString();
}
void CuttlefishConfig::set_uuid(const std::string& uuid) {
  (*dictionary_)[kUuid] = uuid;
}

bool CuttlefishConfig::disable_dac_security() const {
  return (*dictionary_)[kDisableDacSecurity].asBool();
}
void CuttlefishConfig::set_disable_dac_security(bool disable_dac_security) {
  (*dictionary_)[kDisableDacSecurity] = disable_dac_security;
}

void CuttlefishConfig::set_cuttlefish_env_path(const std::string& path) {
  SetPath(kCuttlefishEnvPath, path);
}
std::string CuttlefishConfig::cuttlefish_env_path() const {
  return (*dictionary_)[kCuttlefishEnvPath].asString();
}

bool CuttlefishConfig::disable_app_armor_security() const {
  return (*dictionary_)[kDisableAppArmorSecurity].asBool();
}
void CuttlefishConfig::set_disable_app_armor_security(
    bool disable_app_armor_security) {
  (*dictionary_)[kDisableAppArmorSecurity] = disable_app_armor_security;
}

std::string CuttlefishConfig::adb_mode() const {
  return (*dictionary_)[kAdbMode].asString();
}

void CuttlefishConfig::set_adb_mode(const std::string& mode) {
  (*dictionary_)[kAdbMode] = mode;
}

// Creates the (initially empty) config object and populates it with values from
// the config file if the --config_file command line argument is present.
// Returns nullptr if there was an error loading from file
/*static*/ CuttlefishConfig* CuttlefishConfig::BuildConfigImpl() {
  auto ret = new CuttlefishConfig();
  if (ret && !FLAGS_config_file.empty()) {
    auto loaded = ret->LoadFromFile(FLAGS_config_file.c_str());
    if (!loaded) {
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
  if (HostSupportsQemuCli()) {
    stream << std::getenv("HOME") << "/cuttlefish_runtime";
  } else {
    stream << "/var/run/libvirt-" << kDefaultUuidPrefix << std::setfill('0')
           << std::setw(2) << GetInstance();
  }
  return stream.str();
}

std::string GetDefaultMempath() {
  return GetPerInstanceDefault("/var/run/shm/cvd-");
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
}  // namespace vsoc
