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
#pragma once

#include <memory>
#include <string>

namespace Json {
class Value;
}

namespace vsoc {

// Holds the configuration of the cuttlefish instances.
class CuttlefishConfig {
 public:
  static CuttlefishConfig* Get();
  ~CuttlefishConfig() = default;

  // Saves the configuration object in a file, it can then be read in other
  // processes by passing the --config_file option.
  bool SaveToFile(const std::string& file) const;

  // Returns the path to a file with the given name in the instance directory..
  std::string PerInstancePath(const char* file_name) const;

  std::string instance_name() const;

  void disable_usb_adb() {
    // This seems to be the way usb is being disbled in the launcher
    set_usb_v1_socket_name("");
  }

  std::string instance_dir() const;
  void set_instance_dir(const std::string& instance_dir);

  std::string serial_number() const;
  void set_serial_number(const std::string& serial_number);

  int cpus() const;
  void set_cpus(int cpus);

  int memory_mb() const;
  void set_memory_mb(int memory_mb);

  int dpi() const;
  void set_dpi(int dpi);

  int x_res() const;
  void set_x_res(int x_res);

  int y_res() const;
  void set_y_res(int y_res);

  int refresh_rate_hz() const;
  void set_refresh_rate_hz(int refresh_rate_hz);

  std::string kernel_image_path() const;
  void set_kernel_image_path(const std::string& kernel_image_path);

  std::string kernel_args() const;
  void set_kernel_args(const std::string& kernel_args);

  std::string gdb_flag() const;
  void set_gdb_flag(const std::string& gdb);

  std::string ramdisk_image_path() const;
  void set_ramdisk_image_path(const std::string& ramdisk_image_path);

  std::string system_image_path() const;
  void set_system_image_path(const std::string& system_image_path);

  std::string cache_image_path() const;
  void set_cache_image_path(const std::string& cache_image_path);

  std::string data_image_path() const;
  void set_data_image_path(const std::string& data_image_path);

  std::string vendor_image_path() const;
  void set_vendor_image_path(const std::string& vendor_image_path);

  std::string dtb_path() const;
  void set_dtb_path(const std::string& dtb_path);

  std::string mempath() const;
  void set_mempath(const std::string& mempath);

  std::string ivshmem_qemu_socket_path() const;
  void set_ivshmem_qemu_socket_path(
      const std::string& ivshmem_qemu_socket_path);

  std::string ivshmem_client_socket_path() const;
  void set_ivshmem_client_socket_path(
      const std::string& ivshmem_client_socket_path);

  int ivshmem_vector_count() const;
  void set_ivshmem_vector_count(int ivshmem_vector_count);

  // The name of the socket that will be used to forward access to USB gadget.
  // This is for V1 of the USB bus.
  std::string usb_v1_socket_name() const;
  void set_usb_v1_socket_name(const std::string& usb_v1_socket_name);

  int vhci_port() const;
  void set_vhci_port(int vhci_port);

  std::string usb_ip_socket_name() const;
  void set_usb_ip_socket_name(const std::string& usb_ip_socket_name);

  std::string kernel_log_socket_name() const;
  void set_kernel_log_socket_name(const std::string& kernel_log_socket_name);

  std::string console_path() const;
  void set_console_path(const std::string& console_path);

  std::string logcat_path() const;
  void set_logcat_path(const std::string& logcat_path);

  std::string launcher_log_path() const;
  void set_launcher_log_path(const std::string& launcher_log_path);

  std::string mobile_bridge_name() const;
  void set_mobile_bridge_name(const std::string& mobile_bridge_name);

  std::string mobile_tap_name() const;
  void set_mobile_tap_name(const std::string& mobile_tap_name);

  std::string wifi_bridge_name() const;
  void set_wifi_bridge_name(const std::string& wifi_bridge_name);

  std::string wifi_tap_name() const;
  void set_wifi_tap_name(const std::string& wifi_tap_name);

  std::string wifi_guest_mac_addr() const;
  void set_wifi_guest_mac_addr(const std::string& wifi_guest_mac_addr);

  std::string wifi_host_mac_addr() const;
  void set_wifi_host_mac_addr(const std::string& wifi_host_mac_addr);

  std::string entropy_source() const;
  void set_entropy_source(const std::string& entropy_source);

  std::string uuid() const;
  void set_uuid(const std::string& uuid);

  bool disable_dac_security() const;
  void set_disable_dac_security(bool disable_dac_security);

  bool disable_app_armor_security() const;
  void set_disable_app_armor_security(bool disable_app_armor_security);

  void set_cuttlefish_env_path(const std::string& path);
  std::string cuttlefish_env_path() const;

  void set_adb_mode(const std::string& mode);
  std::string adb_mode() const;

 private:
  std::unique_ptr<Json::Value> dictionary_;

  void LoadFromFile(const char* file);

  CuttlefishConfig();
  CuttlefishConfig(const CuttlefishConfig&) = delete;
  CuttlefishConfig& operator=(const CuttlefishConfig&) = delete;
};

// Returns the instance number as obtained from the CUTTLEFISH_INSTANCE
// environment variable or the username.
int GetInstance();

// Returns the path to the ivserver's client socket.
std::string GetDomain();

// These functions modify a given base value to make it different accross
// different instances by appending the instance id in case of strings or adding
// it in case of integers.
std::string GetPerInstanceDefault(const char* prefix);
int GetPerInstanceDefault(int base);

std::string GetDefaultPerInstanceDir();

std::string DefaultHostArtifactsPath(const std::string& file);
std::string DefaultGuestImagePath(const std::string& file);

// Whether the installed host packages support calling qemu directly instead of
// through libvirt
bool HostSupportsQemuCli();
}  // namespace vsoc
