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
const char* kSerialNumber = "serial_number";
const char* kInstanceDir = "instance_dir";
const char* kDeviceTitle = "device_title";

const char* kVirtualDiskPaths = "virtual_disk_paths";

const char* kMobileBridgeName = "mobile_bridge_name";
const char* kMobileTapName = "mobile_tap_name";
const char* kWifiTapName = "wifi_tap_name";
const char* kVsockGuestCid = "vsock_guest_cid";

const char* kSessionId = "session_id";
const char* kUseAllocd = "use_allocd";

const char* kUuid = "uuid";
const char* kModemSimulatorPorts = "modem_simulator_ports";

const char* kHostPort = "host_port";
const char* kTpmPort = "tpm_port";
const char* kAdbIPAndPort = "adb_ip_and_port";

const char* kConfigServerPort = "config_server_port";
const char* kVncServerPort = "vnc_server_port";
const char* kVehicleHalServerPort = "vehicle_hal_server_port";
const char* kTombstoneReceiverPort = "tombstone_receiver_port";

const char* kWebrtcDeviceId = "webrtc_device_id";
const char* kStartSigServer = "webrtc_start_sig_server";

const char* kFramesServerPort = "frames_server_port";
const char* kTouchServerPort = "touch_server_port";
const char* kKeyboardServerPort = "keyboard_server_port";

const char* kKeymasterVsockPort = "keymaster_vsock_port";
const char* kGatekeeperVsockPort = "gatekeeper_vsock_port";
const char* kWifiMacAddress = "wifi_mac_address";

}  // namespace

Json::Value* CuttlefishConfig::MutableInstanceSpecific::Dictionary() {
  return &(*config_->dictionary_)[kInstances][id_];
}

const Json::Value* CuttlefishConfig::InstanceSpecific::Dictionary() const {
  return &(*config_->dictionary_)[kInstances][id_];
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

std::string CuttlefishConfig::InstanceSpecific::serial_number() const {
  return (*Dictionary())[kSerialNumber].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_serial_number(
    const std::string& serial_number) {
  (*Dictionary())[kSerialNumber] = serial_number;
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
  return cuttlefish::AbsolutePath(PerInstanceInternalPath("kernel-log-pipe"));
}

std::string CuttlefishConfig::InstanceSpecific::console_in_pipe_name() const {
  return cuttlefish::AbsolutePath(PerInstanceInternalPath("console-in-pipe"));
}

std::string CuttlefishConfig::InstanceSpecific::console_out_pipe_name() const {
  return cuttlefish::AbsolutePath(PerInstanceInternalPath("console-out-pipe"));
}

std::string CuttlefishConfig::InstanceSpecific::logcat_pipe_name() const {
  return cuttlefish::AbsolutePath(PerInstanceInternalPath("logcat-pipe"));
}

std::string CuttlefishConfig::InstanceSpecific::access_kregistry_path() const {
  return cuttlefish::AbsolutePath(PerInstancePath("access-kregistry"));
}

std::string CuttlefishConfig::InstanceSpecific::pstore_path() const {
  return cuttlefish::AbsolutePath(PerInstancePath("pstore"));
}

std::string CuttlefishConfig::InstanceSpecific::console_path() const {
  return cuttlefish::AbsolutePath(PerInstancePath("console"));
}

std::string CuttlefishConfig::InstanceSpecific::logcat_path() const {
  return cuttlefish::AbsolutePath(PerInstancePath("logcat"));
}

std::string CuttlefishConfig::InstanceSpecific::launcher_monitor_socket_path()
    const {
  return cuttlefish::AbsolutePath(PerInstancePath("launcher_monitor.sock"));
}

std::string CuttlefishConfig::InstanceSpecific::modem_simulator_ports() const {
  return (*Dictionary())[kModemSimulatorPorts].asString();
}

void CuttlefishConfig::MutableInstanceSpecific::set_modem_simulator_ports(
    const std::string& modem_simulator_ports) {
  (*Dictionary())[kModemSimulatorPorts] = modem_simulator_ports;
}

std::string CuttlefishConfig::InstanceSpecific::launcher_log_path() const {
  return cuttlefish::AbsolutePath(PerInstancePath("launcher.log"));
}

std::string CuttlefishConfig::InstanceSpecific::sdcard_path() const {
  return cuttlefish::AbsolutePath(PerInstancePath("sdcard.img"));
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

bool CuttlefishConfig::InstanceSpecific::use_allocd() const {
  return (*Dictionary())[kUseAllocd].asBool();
}

void CuttlefishConfig::MutableInstanceSpecific::set_use_allocd(
    bool use_allocd) {
  (*Dictionary())[kUseAllocd] = use_allocd;
}

uint32_t CuttlefishConfig::InstanceSpecific::session_id() const {
  return (*Dictionary())[kSessionId].asUInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_session_id(
    uint32_t session_id) {
  (*Dictionary())[kSessionId] = session_id;
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

int CuttlefishConfig::InstanceSpecific::gatekeeper_vsock_port() const {
  return (*Dictionary())[kGatekeeperVsockPort].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_gatekeeper_vsock_port(int gatekeeper_vsock_port) {
  (*Dictionary())[kGatekeeperVsockPort] = gatekeeper_vsock_port;
}

int CuttlefishConfig::InstanceSpecific::tombstone_receiver_port() const {
  return (*Dictionary())[kTombstoneReceiverPort].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_tombstone_receiver_port(int tombstone_receiver_port) {
  (*Dictionary())[kTombstoneReceiverPort] = tombstone_receiver_port;
}

int CuttlefishConfig::InstanceSpecific::vehicle_hal_server_port() const {
  return (*Dictionary())[kVehicleHalServerPort].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_vehicle_hal_server_port(int vehicle_hal_server_port) {
  (*Dictionary())[kVehicleHalServerPort] = vehicle_hal_server_port;
}

int CuttlefishConfig::InstanceSpecific::config_server_port() const {
  return (*Dictionary())[kConfigServerPort].asInt();
}

void CuttlefishConfig::MutableInstanceSpecific::set_config_server_port(int config_server_port) {
  (*Dictionary())[kConfigServerPort] = config_server_port;
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

}  // namespace cuttlefish
