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

#include <array>
#include <memory>
#include <string>
#include <set>
#include <vector>

namespace Json {
class Value;
}

namespace cuttlefish {
constexpr char kLogcatSerialMode[] = "serial";
constexpr char kLogcatVsockMode[] = "vsock";
}

namespace cuttlefish {

constexpr char kDefaultUuidPrefix[] = "699acfc4-c8c4-11e7-882b-5065f31dc1";
constexpr char kCuttlefishConfigEnvVarName[] = "CUTTLEFISH_CONFIG_FILE";
constexpr char kVsocUserPrefix[] = "vsoc-";
constexpr char kBootStartedMessage[] ="VIRTUAL_DEVICE_BOOT_STARTED";
constexpr char kBootCompletedMessage[] = "VIRTUAL_DEVICE_BOOT_COMPLETED";
constexpr char kBootFailedMessage[] = "VIRTUAL_DEVICE_BOOT_FAILED";
constexpr char kMobileNetworkConnectedMessage[] =
    "VIRTUAL_DEVICE_NETWORK_MOBILE_CONNECTED";
constexpr char kWifiConnectedMessage[] =
    "VIRTUAL_DEVICE_NETWORK_WIFI_CONNECTED";
constexpr char kInternalDirName[] = "internal";
constexpr char kCrosvmVarEmptyDir[] = "/var/empty";

enum class AdbMode {
  VsockTunnel,
  VsockHalfTunnel,
  NativeVsock,
  Unknown,
};

// Holds the configuration of the cuttlefish instances.
class CuttlefishConfig {
 public:
  static const CuttlefishConfig* Get();
  static bool ConfigExists();

  CuttlefishConfig();
  CuttlefishConfig(CuttlefishConfig&&);
  ~CuttlefishConfig();
  CuttlefishConfig& operator=(CuttlefishConfig&&);

  // Saves the configuration object in a file, it can then be read in other
  // processes by passing the --config_file option.
  bool SaveToFile(const std::string& file) const;

  std::string assembly_dir() const;
  void set_assembly_dir(const std::string& assembly_dir);

  std::string AssemblyPath(const std::string&) const;

  std::string composite_disk_path() const;

  std::string vm_manager() const;
  void set_vm_manager(const std::string& name);

  std::string gpu_mode() const;
  void set_gpu_mode(const std::string& name);

  std::string serial_number() const;
  void set_serial_number(const std::string& serial_number);

  std::string wayland_socket() const;
  void set_wayland_socket(const std::string& path);

  std::string x_display() const;
  void set_x_display(const std::string& address);

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

  // Returns kernel image extracted from the boot image or the user-provided one
  // if given by command line to the launcher. This function should not be used
  // to get the kernel image the vmm should boot, GetKernelImageToUse() should
  // be used instead.
  std::string kernel_image_path() const;
  void set_kernel_image_path(const std::string& kernel_image_path);

  bool decompress_kernel() const;
  void set_decompress_kernel(bool decompress_kernel);

  // Returns the path to the kernel image that should be given to the vm manager
  // to boot, takes into account whether the original image was decompressed or
  // not.
  std::string GetKernelImageToUse() const {
    return decompress_kernel() ? decompressed_kernel_image_path()
                               : kernel_image_path();
  }

  std::string decompressed_kernel_image_path() const;
  void set_decompressed_kernel_image_path(const std::string& path);

  bool use_unpacked_kernel() const;
  void set_use_unpacked_kernel(bool use_unpacked_kernel);

  std::string gdb_flag() const;
  void set_gdb_flag(const std::string& gdb);

  std::string ramdisk_image_path() const;
  void set_ramdisk_image_path(const std::string& ramdisk_image_path);

  std::string initramfs_path() const;
  void set_initramfs_path(const std::string& initramfs_path);

  std::string final_ramdisk_path() const;
  void set_final_ramdisk_path(const std::string& final_ramdisk_path);

  std::string vendor_ramdisk_image_path() const;
  void set_vendor_ramdisk_image_path(const std::string&
    vendor_ramdisk_image_path);

  bool deprecated_boot_completed() const;
  void set_deprecated_boot_completed(bool deprecated_boot_completed);

  std::string logcat_receiver_binary() const;
  void set_logcat_receiver_binary(const std::string& binary);

  std::string config_server_binary() const;
  void set_config_server_binary(const std::string& binary);

  void set_cuttlefish_env_path(const std::string& path);
  std::string cuttlefish_env_path() const;

  void set_adb_mode(const std::set<std::string>& modes);
  std::set<AdbMode> adb_mode() const;

  void set_setupwizard_mode(const std::string& title);
  std::string setupwizard_mode() const;

  void set_qemu_binary(const std::string& qemu_binary);
  std::string qemu_binary() const;

  void set_crosvm_binary(const std::string& crosvm_binary);
  std::string crosvm_binary() const;

  void set_tpm_binary(const std::string& tpm_binary);
  std::string tpm_binary() const;

  void set_tpm_device(const std::string& tpm_device);
  std::string tpm_device() const;

  void set_console_forwarder_binary(const std::string& crosvm_binary);
  std::string console_forwarder_binary() const;

  void set_kernel_log_monitor_binary(
      const std::string& kernel_log_monitor_binary);
  std::string kernel_log_monitor_binary() const;

  void set_enable_vnc_server(bool enable_vnc_server);
  bool enable_vnc_server() const;

  void set_vnc_server_binary(const std::string& vnc_server_binary);
  std::string vnc_server_binary() const;

  void set_enable_sandbox(const bool enable_sandbox);
  bool enable_sandbox() const;

  void set_seccomp_policy_dir(const std::string& seccomp_policy_dir);
  std::string seccomp_policy_dir() const;

  void set_enable_webrtc(bool enable_webrtc);
  bool enable_webrtc() const;

  void set_webrtc_binary(const std::string& webrtc_binary);
  std::string webrtc_binary() const;

  void set_webrtc_assets_dir(const std::string& webrtc_assets_dir);
  std::string webrtc_assets_dir() const;

  void set_webrtc_public_ip(const std::string& webrtc_public_ip);
  std::string webrtc_public_ip() const;

  void set_webrtc_enable_adb_websocket(bool enable);
  bool webrtc_enable_adb_websocket() const;

  void set_enable_vehicle_hal_grpc_server(bool enable_vhal_server);
  bool enable_vehicle_hal_grpc_server() const;

  void set_vehicle_hal_grpc_server_binary(const std::string& vhal_server_binary);
  std::string vehicle_hal_grpc_server_binary() const;

  void set_restart_subprocesses(bool restart_subprocesses);
  bool restart_subprocesses() const;

  void set_run_adb_connector(bool run_adb_connector);
  bool run_adb_connector() const;

  void set_adb_connector_binary(const std::string& adb_connector_binary);
  std::string adb_connector_binary() const;

  void set_socket_vsock_proxy_binary(const std::string& binary);
  std::string socket_vsock_proxy_binary() const;

  void set_run_as_daemon(bool run_as_daemon);
  bool run_as_daemon() const;

  void set_data_policy(const std::string& data_policy);
  std::string data_policy() const;

  void set_blank_data_image_mb(int blank_data_image_mb);
  int blank_data_image_mb() const;

  void set_blank_data_image_fmt(const std::string& blank_data_image_fmt);
  std::string blank_data_image_fmt() const;

  void set_enable_tombstone_receiver(bool enable_tombstone_receiver);
  bool enable_tombstone_receiver() const;

  void set_tombstone_receiver_binary(const std::string& binary);
  std::string tombstone_receiver_binary() const;

  void set_use_bootloader(bool use_bootloader);
  bool use_bootloader() const;

  void set_bootloader(const std::string& bootloader_path);
  std::string bootloader() const;

  void set_boot_slot(const std::string& boot_slot);
  std::string boot_slot() const;

  void set_loop_max_part(int loop_max_part);
  int loop_max_part() const;

  void set_guest_enforce_security(bool guest_enforce_security);
  bool guest_enforce_security() const;

  void set_guest_audit_security(bool guest_audit_security);
  bool guest_audit_security() const;

  void set_guest_force_normal_boot(bool guest_force_normal_boot);
  bool guest_force_normal_boot() const;

  enum Answer {
    kUnknown = 0,
    kYes,
    kNo,
  };

  void set_enable_metrics(std::string enable_metrics);
  CuttlefishConfig::Answer enable_metrics() const;

  void set_metrics_binary(const std::string& metrics_binary);
  std::string metrics_binary() const;

  void set_boot_image_kernel_cmdline(std::string boot_image_kernel_cmdline);
  std::vector<std::string> boot_image_kernel_cmdline() const;

  void set_extra_kernel_cmdline(std::string extra_cmdline);
  std::vector<std::string> extra_kernel_cmdline() const;

  void set_vm_manager_kernel_cmdline(std::string vm_manager_cmdline);
  std::vector<std::string> vm_manager_kernel_cmdline() const;

  // A directory containing the SSL certificates for the signaling server
  void set_webrtc_certs_dir(const std::string& certs_dir);
  std::string webrtc_certs_dir() const;

  // The path to the webrtc signaling server binary
  void set_sig_server_binary(const std::string& sig_server_binary);
  std::string sig_server_binary() const;

  // The port for the webrtc signaling server. It's used by the signaling server
  // to bind to it and by the webrtc process to connect to and register itself
  void set_sig_server_port(int port);
  int sig_server_port() const;

  // The address of the signaling server
  void set_sig_server_address(const std::string& addr);
  std::string sig_server_address() const;

  // The path section of the url where the webrtc process registers itself with
  // the signaling server
  void set_sig_server_path(const std::string& path);
  std::string sig_server_path() const;

  // Whether the webrtc process should attempt to verify the authenticity of the
  // signaling server (reject self signed certificates)
  void set_sig_server_strict(bool strict);
  bool sig_server_strict() const;

  // The dns address of mobile network (RIL)
  void set_ril_dns(const std::string& ril_dns);
  std::string ril_dns() const;

  // KGDB configuration for kernel debugging
  void set_kgdb(bool kgdb);
  bool kgdb() const;

  class InstanceSpecific;
  class MutableInstanceSpecific;

  MutableInstanceSpecific ForInstance(int instance_num);
  InstanceSpecific ForInstance(int instance_num) const;
  InstanceSpecific ForDefaultInstance() const;

  std::vector<InstanceSpecific> Instances() const;

  // A view into an existing CuttlefishConfig object for a particular instance.
  class InstanceSpecific {
    const CuttlefishConfig* config_;
    std::string id_;
    friend InstanceSpecific CuttlefishConfig::ForInstance(int num) const;
    friend InstanceSpecific CuttlefishConfig::ForDefaultInstance() const;
    friend std::vector<InstanceSpecific> CuttlefishConfig::Instances() const;

    InstanceSpecific(const CuttlefishConfig* config, const std::string& id)
        : config_(config), id_(id) {}

    Json::Value* Dictionary();
    const Json::Value* Dictionary() const;
  public:
    std::string serial_number() const;
    // If any of the following port numbers is 0, the relevant service is not
    // running on the guest.

    // Port number to connect to vnc server on the host
    int vnc_server_port() const;
    // Port number to connect to the tombstone receiver on the host
    int tombstone_receiver_port() const;
    // Port number to connect to the config server on the host
    int config_server_port() const;
    // Port number to connect to the keyboard server on the host. (Only
    // operational if QEMU is the vmm.)
    int keyboard_server_port() const;
    // Port number to connect to the touch server on the host. (Only
    // operational if QEMU is the vmm.)
    int touch_server_port() const;
    // Port number to connect to the frame server on the host. (Only
    // operational if using swiftshader as the GPU.)
    int frames_server_port() const;
    // Port number to connect to the vehicle HAL server on the host
    int vehicle_hal_server_port() const;
    // Port number to connect to the adb server on the host
    int host_port() const;
    // Port number to connect to the tpm server on the host
    int tpm_port() const;
    // Port number to connect to the keymaster server on the host
    int keymaster_vsock_port() const;
    std::string adb_ip_and_port() const;
    std::string adb_device_name() const;
    std::string device_title() const;
    std::string mobile_bridge_name() const;
    std::string mobile_tap_name() const;
    std::string wifi_tap_name() const;
    int vsock_guest_cid() const;
    std::string uuid() const;
    std::string instance_name() const;
    std::vector<std::string> virtual_disk_paths() const;

    // Returns the path to a file with the given name in the instance directory..
    std::string PerInstancePath(const char* file_name) const;
    std::string PerInstanceInternalPath(const char* file_name) const;

    std::string instance_dir() const;

    std::string instance_internal_dir() const;

    std::string touch_socket_path() const;
    std::string keyboard_socket_path() const;
    std::string frames_socket_path() const;

    std::string access_kregistry_path() const;

    std::string pstore_path() const;

    std::string console_path() const;

    std::string logcat_path() const;

    std::string kernel_log_pipe_name() const;

    std::string console_pipe_name() const;

    std::string logcat_pipe_name() const;

    std::string launcher_log_path() const;

    std::string launcher_monitor_socket_path() const;

    std::string sdcard_path() const;

    // The device id the webrtc process should use to register with the
    // signaling server
    std::string webrtc_device_id() const;

    // Whether this instance should start the webrtc signaling server
    bool start_webrtc_sig_server() const;

    // Wifi MAC address inside the guest
    std::array<unsigned char, 6> wifi_mac_address() const;
  };

  // A view into an existing CuttlefishConfig object for a particular instance.
  class MutableInstanceSpecific {
    CuttlefishConfig* config_;
    std::string id_;
    friend MutableInstanceSpecific CuttlefishConfig::ForInstance(int num);

    MutableInstanceSpecific(CuttlefishConfig* config, const std::string& id)
        : config_(config), id_(id) {}

    Json::Value* Dictionary();
  public:
    void set_serial_number(const std::string& serial_number);
    void set_vnc_server_port(int vnc_server_port);
    void set_tombstone_receiver_port(int tombstone_receiver_port);
    void set_config_server_port(int config_server_port);
    void set_frames_server_port(int config_server_port);
    void set_touch_server_port(int config_server_port);
    void set_keyboard_server_port(int config_server_port);
    void set_keymaster_vsock_port(int keymaster_vsock_port);
    void set_vehicle_hal_server_port(int vehicle_server_port);
    void set_host_port(int host_port);
    void set_tpm_port(int tpm_port);
    void set_adb_ip_and_port(const std::string& ip_port);
    void set_device_title(const std::string& title);
    void set_mobile_bridge_name(const std::string& mobile_bridge_name);
    void set_mobile_tap_name(const std::string& mobile_tap_name);
    void set_wifi_tap_name(const std::string& wifi_tap_name);
    void set_vsock_guest_cid(int vsock_guest_cid);
    void set_uuid(const std::string& uuid);
    void set_instance_dir(const std::string& instance_dir);
    void set_virtual_disk_paths(const std::vector<std::string>& disk_paths);
    void set_webrtc_device_id(const std::string& id);
    void set_start_webrtc_signaling_server(bool start);
    // Wifi MAC address inside the guest
    void set_wifi_mac_address(const std::array<unsigned char, 6>&);
  };

 private:
  std::unique_ptr<Json::Value> dictionary_;

  void SetPath(const std::string& key, const std::string& path);
  bool LoadFromFile(const char* file);
  static CuttlefishConfig* BuildConfigImpl();

  CuttlefishConfig(const CuttlefishConfig&) = delete;
  CuttlefishConfig& operator=(const CuttlefishConfig&) = delete;
};

// Returns the instance number as obtained from the CUTTLEFISH_INSTANCE
// environment variable or the username.
int GetInstance();
// Returns a path where the launhcer puts a link to the config file which makes
// it easily discoverable regardless of what vm manager is in use
std::string GetGlobalConfigFileLink();

// These functions modify a given base value to make it different accross
// different instances by appending the instance id in case of strings or adding
// it in case of integers.
std::string ForCurrentInstance(const char* prefix);
int ForCurrentInstance(int base);

// Returns a random serial number appeneded to a given prefix.
std::string RandomSerialNumber(const std::string& prefix);

std::string GetDefaultMempath();
int GetDefaultPerInstanceVsockCid();

std::string DefaultHostArtifactsPath(const std::string& file);
std::string DefaultGuestImagePath(const std::string& file);
std::string DefaultEnvironmentPath(const char* environment_key,
                                   const char* default_value,
                                   const char* path);

// Whether the host supports qemu
bool HostSupportsQemuCli();
bool HostSupportsVsock();

// GPU modes
extern const char* const kGpuModeGuestSwiftshader;
extern const char* const kGpuModeDrmVirgl;
extern const char* const kGpuModeGfxStream;
}  // namespace cuttlefish
