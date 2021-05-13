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

#include <android-base/logging.h>
#include <json/json.h>

#include "common/libs/utils/files.h"

namespace cuttlefish {
namespace {

const char* kInstances = "instances";

}  // namespace

Json::Value* CuttlefishConfig::MutableInstanceSpecific::Dictionary() {
  return &(*config_->dictionary_)[kInstances][id_];
}

const Json::Value* CuttlefishConfig::InstanceSpecific::Dictionary() const {
  return &(*config_->dictionary_)[kInstances][id_];
}

static constexpr char kInstanceDir[] = "instance_dir";
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

static constexpr char kSerialNumber[] = "serial_number";
std::string CuttlefishConfig::InstanceSpecific::serial_number() const {
  return (*Dictionary())[kSerialNumber].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_serial_number(
    const std::string& serial_number) {
  (*Dictionary())[kSerialNumber] = serial_number;
}

static constexpr char kVirtualDiskPaths[] = "virtual_disk_paths";
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
  return AbsolutePath(PerInstanceInternalPath("kernel-log-pipe"));
}

std::string CuttlefishConfig::InstanceSpecific::console_pipe_prefix() const {
  return AbsolutePath(PerInstanceInternalPath("console"));
}

std::string CuttlefishConfig::InstanceSpecific::console_in_pipe_name() const {
  return console_pipe_prefix() + ".in";
}

std::string CuttlefishConfig::InstanceSpecific::console_out_pipe_name() const {
  return console_pipe_prefix() + ".out";
}

std::string CuttlefishConfig::InstanceSpecific::gnss_pipe_prefix() const {
  return AbsolutePath(PerInstanceInternalPath("gnss"));
}

std::string CuttlefishConfig::InstanceSpecific::gnss_in_pipe_name() const {
  return gnss_pipe_prefix() + ".in";
}

std::string CuttlefishConfig::InstanceSpecific::gnss_out_pipe_name() const {
  return gnss_pipe_prefix() + ".out";
}

static constexpr char kGnssGrpcProxyServerPort[] =
    "gnss_grpc_proxy_server_port";
int CuttlefishConfig::InstanceSpecific::gnss_grpc_proxy_server_port() const {
  return (*Dictionary())[kGnssGrpcProxyServerPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_gnss_grpc_proxy_server_port(
    int gnss_grpc_proxy_server_port) {
  (*Dictionary())[kGnssGrpcProxyServerPort] = gnss_grpc_proxy_server_port;
}

static constexpr char kGnssFilePath[] = "gnss_file_path";
std::string CuttlefishConfig::InstanceSpecific::gnss_file_path() const {
  return (*Dictionary())[kGnssFilePath].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_gnss_file_path(
  const std::string& gnss_file_path) {
  (*Dictionary())[kGnssFilePath] = gnss_file_path;
}

std::string CuttlefishConfig::InstanceSpecific::logcat_pipe_name() const {
  return AbsolutePath(PerInstanceInternalPath("logcat-pipe"));
}

std::string CuttlefishConfig::InstanceSpecific::access_kregistry_path() const {
  return AbsolutePath(PerInstancePath("access-kregistry"));
}

std::string CuttlefishConfig::InstanceSpecific::pstore_path() const {
  return AbsolutePath(PerInstancePath("pstore"));
}

std::string CuttlefishConfig::InstanceSpecific::console_path() const {
  return AbsolutePath(PerInstancePath("console"));
}

std::string CuttlefishConfig::InstanceSpecific::logcat_path() const {
  return AbsolutePath(PerInstancePath("logcat"));
}

std::string CuttlefishConfig::InstanceSpecific::launcher_monitor_socket_path()
    const {
  return AbsolutePath(PerInstancePath("launcher_monitor.sock"));
}

static constexpr char kModemSimulatorPorts[] = "modem_simulator_ports";
std::string CuttlefishConfig::InstanceSpecific::modem_simulator_ports() const {
  return (*Dictionary())[kModemSimulatorPorts].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_modem_simulator_ports(
    const std::string& modem_simulator_ports) {
  (*Dictionary())[kModemSimulatorPorts] = modem_simulator_ports;
}

std::string CuttlefishConfig::InstanceSpecific::launcher_log_path() const {
  return AbsolutePath(PerInstancePath("launcher.log"));
}

std::string CuttlefishConfig::InstanceSpecific::sdcard_path() const {
  return AbsolutePath(PerInstancePath("sdcard.img"));
}

std::string CuttlefishConfig::InstanceSpecific::os_composite_disk_path() const {
  return AbsolutePath(PerInstancePath("os_composite.img"));
}

std::string CuttlefishConfig::InstanceSpecific::persistent_composite_disk_path()
    const {
  return AbsolutePath(PerInstancePath("persistent_composite.img"));
}

std::string CuttlefishConfig::InstanceSpecific::uboot_env_image_path() const {
  return AbsolutePath(PerInstancePath("uboot_env.img"));
}

std::string CuttlefishConfig::InstanceSpecific::vendor_boot_image_path() const {
  return AbsolutePath(PerInstancePath("vendor_boot_repacked.img"));
}

static constexpr char kMobileBridgeName[] = "mobile_bridge_name";

std::string CuttlefishConfig::InstanceSpecific::audio_server_path() const {
  return AbsolutePath(PerInstanceInternalPath("audio_server.sock"));
}

std::string CuttlefishConfig::InstanceSpecific::mobile_bridge_name() const {
  return (*Dictionary())[kMobileBridgeName].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_mobile_bridge_name(
    const std::string& mobile_bridge_name) {
  (*Dictionary())[kMobileBridgeName] = mobile_bridge_name;
}

static constexpr char kMobileTapName[] = "mobile_tap_name";
std::string CuttlefishConfig::InstanceSpecific::mobile_tap_name() const {
  return (*Dictionary())[kMobileTapName].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_mobile_tap_name(
    const std::string& mobile_tap_name) {
  (*Dictionary())[kMobileTapName] = mobile_tap_name;
}

std::string CuttlefishConfig::InstanceSpecific::confui_hal_guest_socket_path()
    const {
  return PerInstanceInternalPath("confui_mock_hal_guest.sock");
}

static constexpr char kWifiTapName[] = "wifi_tap_name";
std::string CuttlefishConfig::InstanceSpecific::wifi_tap_name() const {
  return (*Dictionary())[kWifiTapName].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_wifi_tap_name(
    const std::string& wifi_tap_name) {
  (*Dictionary())[kWifiTapName] = wifi_tap_name;
}

static constexpr char kEthernetTapName[] = "ethernet_tap_name";
std::string CuttlefishConfig::InstanceSpecific::ethernet_tap_name() const {
  return (*Dictionary())[kEthernetTapName].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_ethernet_tap_name(
    const std::string& ethernet_tap_name) {
  (*Dictionary())[kEthernetTapName] = ethernet_tap_name;
}

static constexpr char kUseAllocd[] = "use_allocd";
bool CuttlefishConfig::InstanceSpecific::use_allocd() const {
  return (*Dictionary())[kUseAllocd].asBool();
}
void CuttlefishConfig::MutableInstanceSpecific::set_use_allocd(
    bool use_allocd) {
  (*Dictionary())[kUseAllocd] = use_allocd;
}

static constexpr char kSessionId[] = "session_id";
uint32_t CuttlefishConfig::InstanceSpecific::session_id() const {
  return (*Dictionary())[kSessionId].asUInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_session_id(
    uint32_t session_id) {
  (*Dictionary())[kSessionId] = session_id;
}

static constexpr char kVsockGuestCid[] = "vsock_guest_cid";
int CuttlefishConfig::InstanceSpecific::vsock_guest_cid() const {
  return (*Dictionary())[kVsockGuestCid].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_vsock_guest_cid(
    int vsock_guest_cid) {
  (*Dictionary())[kVsockGuestCid] = vsock_guest_cid;
}

static constexpr char kUuid[] = "uuid";
std::string CuttlefishConfig::InstanceSpecific::uuid() const {
  return (*Dictionary())[kUuid].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_uuid(const std::string& uuid) {
  (*Dictionary())[kUuid] = uuid;
}

static constexpr char kHostPort[] = "host_port";
int CuttlefishConfig::InstanceSpecific::host_port() const {
  return (*Dictionary())[kHostPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_host_port(int host_port) {
  (*Dictionary())[kHostPort] = host_port;
}

static constexpr char kAdbIPAndPort[] = "adb_ip_and_port";
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

static constexpr char kDeviceTitle[] = "device_title";
std::string CuttlefishConfig::InstanceSpecific::device_title() const {
  return (*Dictionary())[kDeviceTitle].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_device_title(
    const std::string& title) {
  (*Dictionary())[kDeviceTitle] = title;
}

static constexpr char kVncServerPort[] = "vnc_server_port";
int CuttlefishConfig::InstanceSpecific::vnc_server_port() const {
  return (*Dictionary())[kVncServerPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_vnc_server_port(int vnc_server_port) {
  (*Dictionary())[kVncServerPort] = vnc_server_port;
}

static constexpr char kFramesServerPort[] = "frames_server_port";
int CuttlefishConfig::InstanceSpecific::frames_server_port() const {
  return (*Dictionary())[kFramesServerPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_frames_server_port(int frames_server_port) {
  (*Dictionary())[kFramesServerPort] = frames_server_port;
}

static constexpr char kTouchServerPort[] = "touch_server_port";
int CuttlefishConfig::InstanceSpecific::touch_server_port() const {
  return (*Dictionary())[kTouchServerPort].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_touch_server_port(int touch_server_port) {
  (*Dictionary())[kTouchServerPort] = touch_server_port;
}

static constexpr char kKeyboardServerPort[] = "keyboard_server_port";
int CuttlefishConfig::InstanceSpecific::keyboard_server_port() const {
  return (*Dictionary())[kKeyboardServerPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_keyboard_server_port(int keyboard_server_port) {
  (*Dictionary())[kKeyboardServerPort] = keyboard_server_port;
}

static constexpr char kTombstoneReceiverPort[] = "tombstone_receiver_port";
int CuttlefishConfig::InstanceSpecific::tombstone_receiver_port() const {
  return (*Dictionary())[kTombstoneReceiverPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_tombstone_receiver_port(int tombstone_receiver_port) {
  (*Dictionary())[kTombstoneReceiverPort] = tombstone_receiver_port;
}

static constexpr char kVehicleHalServerPort[] = "vehicle_hal_server_port";
int CuttlefishConfig::InstanceSpecific::vehicle_hal_server_port() const {
  return (*Dictionary())[kVehicleHalServerPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_vehicle_hal_server_port(int vehicle_hal_server_port) {
  (*Dictionary())[kVehicleHalServerPort] = vehicle_hal_server_port;
}

static constexpr char kAudioControlServerPort[] = "audiocontrol_server_port";
int CuttlefishConfig::InstanceSpecific::audiocontrol_server_port() const {
  return (*Dictionary())[kAudioControlServerPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_audiocontrol_server_port(int audiocontrol_server_port) {
  (*Dictionary())[kAudioControlServerPort] = audiocontrol_server_port;
}

static constexpr char kConfigServerPort[] = "config_server_port";
int CuttlefishConfig::InstanceSpecific::config_server_port() const {
  return (*Dictionary())[kConfigServerPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_config_server_port(int config_server_port) {
  (*Dictionary())[kConfigServerPort] = config_server_port;
}

static constexpr char kRootcanalHciPort[] = "rootcanal_hci_port";
int CuttlefishConfig::InstanceSpecific::rootcanal_hci_port() const {
  return (*Dictionary())[kRootcanalHciPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_rootcanal_hci_port(
    int rootcanal_hci_port) {
  (*Dictionary())[kRootcanalHciPort] = rootcanal_hci_port;
}

static constexpr char kRootcanalLinkPort[] = "rootcanal_link_port";
int CuttlefishConfig::InstanceSpecific::rootcanal_link_port() const {
  return (*Dictionary())[kRootcanalLinkPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_rootcanal_link_port(
    int rootcanal_link_port) {
  (*Dictionary())[kRootcanalLinkPort] = rootcanal_link_port;
}

static constexpr char kRootcanalTestPort[] = "rootcanal_test_port";
int CuttlefishConfig::InstanceSpecific::rootcanal_test_port() const {
  return (*Dictionary())[kRootcanalTestPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_rootcanal_test_port(
    int rootcanal_test_port) {
  (*Dictionary())[kRootcanalTestPort] = rootcanal_test_port;
}

static constexpr char kRootcanalConfigFile[] = "rootcanal_config_file";
std::string CuttlefishConfig::InstanceSpecific::rootcanal_config_file() const {
  return (*Dictionary())[kRootcanalConfigFile].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_rootcanal_config_file(
    const std::string& rootcanal_config_file) {
  (*Dictionary())[kRootcanalConfigFile] =
      DefaultHostArtifactsPath(rootcanal_config_file);
}

static constexpr char kRootcanalDefaultCommandsFile[] =
    "rootcanal_default_commands_file";
std::string
CuttlefishConfig::InstanceSpecific::rootcanal_default_commands_file() const {
  return (*Dictionary())[kRootcanalDefaultCommandsFile].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::
    set_rootcanal_default_commands_file(
        const std::string& rootcanal_default_commands_file) {
  (*Dictionary())[kRootcanalDefaultCommandsFile] =
      DefaultHostArtifactsPath(rootcanal_default_commands_file);
}

static constexpr char kWebrtcDeviceId[] = "webrtc_device_id";
void CuttlefishConfig::MutableInstanceSpecific::set_webrtc_device_id(
    const std::string& id) {
  (*Dictionary())[kWebrtcDeviceId] = id;
}
std::string CuttlefishConfig::InstanceSpecific::webrtc_device_id() const {
  return (*Dictionary())[kWebrtcDeviceId].asString();
}

static constexpr char kStartSigServer[] = "webrtc_start_sig_server";
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

std::string CuttlefishConfig::InstanceSpecific::switches_socket_path() const {
  return PerInstanceInternalPath("switches.sock");
}

std::string CuttlefishConfig::InstanceSpecific::frames_socket_path() const {
  return PerInstanceInternalPath("frames.sock");
}

static constexpr char kWifiMacAddress[] = "wifi_mac_address";
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

std::string CuttlefishConfig::InstanceSpecific::factory_reset_protected_path() const {
  return PerInstanceInternalPath("factory_reset_protected.img");
}

std::string CuttlefishConfig::InstanceSpecific::persistent_bootconfig_path()
    const {
  return PerInstanceInternalPath("bootconfig");
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
  return "cvd-" + id_;
}

}  // namespace cuttlefish
