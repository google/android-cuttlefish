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

#include <sys/types.h>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <set>
#include <vector>

#include "common/libs/utils/environment.h"
#include "host/libs/config/config_fragment.h"

namespace Json {
class Value;
}

namespace cuttlefish {
constexpr char kLogcatSerialMode[] = "serial";
constexpr char kLogcatVsockMode[] = "vsock";

constexpr char kDefaultUuidPrefix[] = "699acfc4-c8c4-11e7-882b-5065f31dc1";
constexpr char kCuttlefishConfigEnvVarName[] = "CUTTLEFISH_CONFIG_FILE";
constexpr char kVsocUserPrefix[] = "vsoc-";
constexpr char kCvdNamePrefix[] = "cvd-";
constexpr char kBootStartedMessage[] ="VIRTUAL_DEVICE_BOOT_STARTED";
constexpr char kBootCompletedMessage[] = "VIRTUAL_DEVICE_BOOT_COMPLETED";
constexpr char kBootFailedMessage[] = "VIRTUAL_DEVICE_BOOT_FAILED";
constexpr char kMobileNetworkConnectedMessage[] =
    "VIRTUAL_DEVICE_NETWORK_MOBILE_CONNECTED";
constexpr char kWifiConnectedMessage[] =
    "VIRTUAL_DEVICE_NETWORK_WIFI_CONNECTED";
constexpr char kEthernetConnectedMessage[] =
    "VIRTUAL_DEVICE_NETWORK_ETHERNET_CONNECTED";
constexpr char kScreenChangedMessage[] = "VIRTUAL_DEVICE_SCREEN_CHANGED";
constexpr char kDisplayPowerModeChangedMessage[] =
    "VIRTUAL_DEVICE_DISPLAY_POWER_MODE_CHANGED";
constexpr char kInternalDirName[] = "internal";
constexpr char kSharedDirName[] = "shared";
constexpr char kLogDirName[] = "logs";
constexpr char kCrosvmVarEmptyDir[] = "/var/empty";
constexpr char kKernelLoadedMessage[] = "] Linux version";
constexpr char kBootloaderLoadedMessage[] = "U-Boot 20";

enum class SecureHal {
  Unknown,
  Keymint,
  Gatekeeper,
};

// Holds the configuration of the cuttlefish instances.
class CuttlefishConfig {
 public:
  static const CuttlefishConfig* Get();
  static std::unique_ptr<const CuttlefishConfig> GetFromFile(
      const std::string& path);
  static bool ConfigExists();

  CuttlefishConfig();
  CuttlefishConfig(CuttlefishConfig&&);
  ~CuttlefishConfig();
  CuttlefishConfig& operator=(CuttlefishConfig&&);

  // Saves the configuration object in a file, it can then be read in other
  // processes by passing the --config_file option.
  bool SaveToFile(const std::string& file) const;

  bool SaveFragment(const ConfigFragment&);
  bool LoadFragment(ConfigFragment&) const;

  std::string root_dir() const;
  void set_root_dir(const std::string& root_dir);

  std::string instances_dir() const;
  std::string InstancesPath(const std::string&) const;

  std::string assembly_dir() const;
  std::string AssemblyPath(const std::string&) const;

  std::string os_composite_disk_path() const;

  std::string vm_manager() const;
  void set_vm_manager(const std::string& name);

  std::string gpu_mode() const;
  void set_gpu_mode(const std::string& name);

  std::string gpu_capture_binary() const;
  void set_gpu_capture_binary(const std::string&);

  std::string hwcomposer() const;
  void set_hwcomposer(const std::string&);

  void set_enable_gpu_udmabuf(const bool enable_gpu_udmabuf);
  bool enable_gpu_udmabuf() const;

  void set_enable_gpu_angle(const bool enable_gpu_angle);
  bool enable_gpu_angle() const;

  int cpus() const;
  void set_cpus(int cpus);

  int memory_mb() const;
  void set_memory_mb(int memory_mb);

  struct DisplayConfig {
    int width;
    int height;
    int dpi;
    int refresh_rate_hz;
  };

  std::vector<DisplayConfig> display_configs() const;
  void set_display_configs(const std::vector<DisplayConfig>& display_configs);

  int gdb_port() const;
  void set_gdb_port(int gdb_port);

  bool deprecated_boot_completed() const;
  void set_deprecated_boot_completed(bool deprecated_boot_completed);

  void set_cuttlefish_env_path(const std::string& path);
  std::string cuttlefish_env_path() const;

  void set_secure_hals(const std::set<std::string>& hals);
  std::set<SecureHal> secure_hals() const;

  void set_setupwizard_mode(const std::string& title);
  std::string setupwizard_mode() const;

  void set_qemu_binary_dir(const std::string& qemu_binary_dir);
  std::string qemu_binary_dir() const;

  void set_crosvm_binary(const std::string& crosvm_binary);
  std::string crosvm_binary() const;

  void set_gem5_binary_dir(const std::string& gem5_binary_dir);
  std::string gem5_binary_dir() const;

  void set_enable_sandbox(const bool enable_sandbox);
  bool enable_sandbox() const;

  void set_seccomp_policy_dir(const std::string& seccomp_policy_dir);
  std::string seccomp_policy_dir() const;

  void set_enable_webrtc(bool enable_webrtc);
  bool enable_webrtc() const;

  void set_webrtc_assets_dir(const std::string& webrtc_assets_dir);
  std::string webrtc_assets_dir() const;

  void set_webrtc_enable_adb_websocket(bool enable);
  bool webrtc_enable_adb_websocket() const;

  void set_enable_vehicle_hal_grpc_server(bool enable_vhal_server);
  bool enable_vehicle_hal_grpc_server() const;

  void set_restart_subprocesses(bool restart_subprocesses);
  bool restart_subprocesses() const;

  void set_enable_gnss_grpc_proxy(const bool enable_gnss_grpc_proxy);
  bool enable_gnss_grpc_proxy() const;

  void set_run_as_daemon(bool run_as_daemon);
  bool run_as_daemon() const;

  void set_data_policy(const std::string& data_policy);
  std::string data_policy() const;

  void set_blank_data_image_mb(int blank_data_image_mb);
  int blank_data_image_mb() const;

  void set_bootloader(const std::string& bootloader_path);
  std::string bootloader() const;

  // TODO (b/163575714) add virtio console support to the bootloader so the
  // virtio console path for the console device can be taken again. When that
  // happens, this function can be deleted along with all the code paths it
  // forces.
  bool use_bootloader() const { return true; };

  void set_boot_slot(const std::string& boot_slot);
  std::string boot_slot() const;

  void set_guest_enforce_security(bool guest_enforce_security);
  bool guest_enforce_security() const;

  void set_enable_host_bluetooth(bool enable_host_bluetooth);
  bool enable_host_bluetooth() const;

  enum Answer {
    kUnknown = 0,
    kYes,
    kNo,
  };

  void set_enable_metrics(std::string enable_metrics);
  CuttlefishConfig::Answer enable_metrics() const;

  void set_metrics_binary(const std::string& metrics_binary);
  std::string metrics_binary() const;

  void set_extra_kernel_cmdline(const std::string& extra_cmdline);
  std::vector<std::string> extra_kernel_cmdline() const;

  void set_extra_bootconfig_args(const std::string& extra_bootconfig_args);
  std::vector<std::string> extra_bootconfig_args() const;

  // A directory containing the SSL certificates for the signaling server
  void set_webrtc_certs_dir(const std::string& certs_dir);
  std::string webrtc_certs_dir() const;

  // The port for the webrtc signaling server. It's used by the signaling server
  // to bind to it and by the webrtc process to connect to and register itself
  void set_sig_server_port(int port);
  int sig_server_port() const;

  // The range of UDP ports available for webrtc sessions.
  void set_webrtc_udp_port_range(std::pair<uint16_t, uint16_t> range);
  std::pair<uint16_t, uint16_t> webrtc_udp_port_range() const;

  // The range of TCP ports available for webrtc sessions.
  void set_webrtc_tcp_port_range(std::pair<uint16_t, uint16_t> range);
  std::pair<uint16_t, uint16_t> webrtc_tcp_port_range() const;

  // The address of the signaling server
  void set_sig_server_address(const std::string& addr);
  std::string sig_server_address() const;

  // The path section of the url where the webrtc process registers itself with
  // the signaling server
  void set_sig_server_path(const std::string& path);
  std::string sig_server_path() const;

  // Whether the webrtc process should use a secure connection (WSS) to the
  // signaling server.
  void set_sig_server_secure(bool secure);
  bool sig_server_secure() const;

  // Whether the webrtc process should attempt to verify the authenticity of the
  // signaling server (reject self signed certificates)
  void set_sig_server_strict(bool strict);
  bool sig_server_strict() const;

  // A file containing http headers to include in the connection to the
  // signaling server
  void set_sig_server_headers_path(const std::string& path);
  std::string sig_server_headers_path() const;

  // The dns address of mobile network (RIL)
  void set_ril_dns(const std::string& ril_dns);
  std::string ril_dns() const;

  // KGDB configuration for kernel debugging
  void set_kgdb(bool kgdb);
  bool kgdb() const;

  // Serial console
  void set_console(bool console);
  bool console() const;
  std::string console_dev() const;

  // Configuration flags for a minimal device
  bool enable_minimal_mode() const;
  void set_enable_minimal_mode(bool enable_minimal_mode);

  void set_enable_modem_simulator(bool enable_modem_simulator);
  bool enable_modem_simulator() const;

  void set_modem_simulator_instance_number(int instance_numbers);
  int modem_simulator_instance_number() const;

  void set_modem_simulator_sim_type(int sim_type);
  int modem_simulator_sim_type() const;

  void set_host_tools_version(const std::map<std::string, uint32_t>&);
  std::map<std::string, uint32_t> host_tools_version() const;

  void set_vhost_net(bool vhost_net);
  bool vhost_net() const;

  void set_vhost_user_mac80211_hwsim(const std::string& path);
  std::string vhost_user_mac80211_hwsim() const;

  void set_wmediumd_api_server_socket(const std::string& path);
  std::string wmediumd_api_server_socket() const;

  void set_ap_rootfs_image(const std::string& path);
  std::string ap_rootfs_image() const;

  void set_ap_kernel_image(const std::string& path);
  std::string ap_kernel_image() const;

  void set_wmediumd_config(const std::string& path);
  std::string wmediumd_config() const;

  void set_rootcanal_hci_port(int rootcanal_hci_port);
  int rootcanal_hci_port() const;

  void set_rootcanal_link_port(int rootcanal_link_port);
  int rootcanal_link_port() const;

  void set_rootcanal_test_port(int rootcanal_test_port);
  int rootcanal_test_port() const;

  void set_rootcanal_config_file(const std::string& rootcanal_config_file);
  std::string rootcanal_config_file() const;

  void set_rootcanal_default_commands_file(
      const std::string& rootcanal_default_commands_file);
  std::string rootcanal_default_commands_file() const;

  void set_record_screen(bool record_screen);
  bool record_screen() const;

  void set_smt(bool smt);
  bool smt() const;

  void set_enable_audio(bool enable);
  bool enable_audio() const;

  void set_protected_vm(bool protected_vm);
  bool protected_vm() const;

  void set_target_arch(Arch target_arch);
  Arch target_arch() const;

  void set_bootconfig_supported(bool bootconfig_supported);
  bool bootconfig_supported() const;

  void set_userdata_format(const std::string& userdata_format);
  std::string userdata_format() const;

  // The path of an AP image in composite disk
  std::string ap_image_dev_path() const;
  void set_ap_image_dev_path(const std::string& dev_path);

  class InstanceSpecific;
  class MutableInstanceSpecific;

  MutableInstanceSpecific ForInstance(int instance_num);
  InstanceSpecific ForInstance(int instance_num) const;
  InstanceSpecific ForInstanceName(const std::string& name) const;
  InstanceSpecific ForDefaultInstance() const;

  std::vector<InstanceSpecific> Instances() const;
  std::vector<std::string> instance_dirs() const;

  void set_instance_names(const std::vector<std::string>& instance_names);
  std::vector<std::string> instance_names() const;

  // A view into an existing CuttlefishConfig object for a particular instance.
  class InstanceSpecific {
    const CuttlefishConfig* config_;
    std::string id_;
    friend InstanceSpecific CuttlefishConfig::ForInstance(int num) const;
    friend std::vector<InstanceSpecific> CuttlefishConfig::Instances() const;

    InstanceSpecific(const CuttlefishConfig* config, const std::string& id)
        : config_(config), id_(id) {}

    Json::Value* Dictionary();
    const Json::Value* Dictionary() const;
  public:
    std::string serial_number() const;
    // If any of the following port numbers is 0, the relevant service is not
    // running on the guest.

    // Port number for qemu to run a vnc server on the host
    int qemu_vnc_server_port() const;
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
    // Port number to connect to the vehicle HAL server on the host
    int vehicle_hal_server_port() const;
    // Port number to connect to the audiocontrol server on the guest
    int audiocontrol_server_port() const;
    // Port number to connect to the adb server on the host
    int adb_host_port() const;
    // Device-specific ID to distinguish modem simulators. Must be 4 digits.
    int modem_simulator_host_id() const;
    // Port number to connect to the gnss grpc proxy server on the host
    int gnss_grpc_proxy_server_port() const;
    std::string adb_ip_and_port() const;
    // Port number to connect to the camera hal on the guest
    int camera_server_port() const;

    std::string adb_device_name() const;
    std::string gnss_file_path() const;
    std::string mobile_bridge_name() const;
    std::string mobile_tap_name() const;
    std::string wifi_tap_name() const;
    std::string ethernet_tap_name() const;
    uint32_t session_id() const;
    bool use_allocd() const;
    int vsock_guest_cid() const;
    std::string uuid() const;
    std::string instance_name() const;
    std::vector<std::string> virtual_disk_paths() const;

    // Returns the path to a file with the given name in the instance
    // directory..
    std::string PerInstancePath(const char* file_name) const;
    std::string PerInstanceInternalPath(const char* file_name) const;
    std::string PerInstanceLogPath(const std::string& file_name) const;

    std::string instance_dir() const;

    std::string instance_internal_dir() const;

    std::string touch_socket_path(int screen_idx) const;
    std::string keyboard_socket_path() const;
    std::string switches_socket_path() const;
    std::string frames_socket_path() const;

    int confui_host_vsock_port() const;

    std::string access_kregistry_path() const;

    std::string hwcomposer_pmem_path() const;

    std::string pstore_path() const;

    std::string console_path() const;

    std::string logcat_path() const;

    std::string kernel_log_pipe_name() const;

    std::string console_pipe_prefix() const;
    std::string console_in_pipe_name() const;
    std::string console_out_pipe_name() const;

    std::string gnss_pipe_prefix() const;
    std::string gnss_in_pipe_name() const;
    std::string gnss_out_pipe_name() const;

    std::string logcat_pipe_name() const;

    std::string launcher_log_path() const;

    std::string launcher_monitor_socket_path() const;

    std::string sdcard_path() const;

    std::string persistent_composite_disk_path() const;

    std::string uboot_env_image_path() const;

    std::string audio_server_path() const;

    // modem simulator related
    std::string modem_simulator_ports() const;

    // The device id the webrtc process should use to register with the
    // signaling server
    std::string webrtc_device_id() const;

    // Whether this instance should start the webrtc signaling server
    bool start_webrtc_sig_server() const;

    // Whether to start a reverse proxy to the webrtc signaling server already
    // running in the host
    bool start_webrtc_sig_server_proxy() const;

    // Whether this instance should start the wmediumd process
    bool start_wmediumd() const;

    // Whether this instance should start a rootcanal instance
    bool start_rootcanal() const;

    // Whether this instance should start an ap instance
    bool start_ap() const;

    // Wifi MAC address inside the guest
    int wifi_mac_prefix() const;

    std::string factory_reset_protected_path() const;

    std::string persistent_bootconfig_path() const;

    std::string vbmeta_path() const;

    std::string id() const;
  };

  // A view into an existing CuttlefishConfig object for a particular instance.
  class MutableInstanceSpecific {
    CuttlefishConfig* config_;
    std::string id_;
    friend MutableInstanceSpecific CuttlefishConfig::ForInstance(int num);

    MutableInstanceSpecific(CuttlefishConfig* config, const std::string& id);

    Json::Value* Dictionary();
  public:
    void set_serial_number(const std::string& serial_number);
    void set_qemu_vnc_server_port(int qemu_vnc_server_port);
    void set_tombstone_receiver_port(int tombstone_receiver_port);
    void set_config_server_port(int config_server_port);
    void set_frames_server_port(int config_server_port);
    void set_touch_server_port(int config_server_port);
    void set_keyboard_server_port(int config_server_port);
    void set_gatekeeper_vsock_port(int gatekeeper_vsock_port);
    void set_keymaster_vsock_port(int keymaster_vsock_port);
    void set_vehicle_hal_server_port(int vehicle_server_port);
    void set_audiocontrol_server_port(int audiocontrol_server_port);
    void set_adb_host_port(int adb_host_port);
    void set_modem_simulator_host_id(int modem_simulator_id);
    void set_adb_ip_and_port(const std::string& ip_port);
    void set_confui_host_vsock_port(int confui_host_port);
    void set_camera_server_port(int camera_server_port);
    void set_mobile_bridge_name(const std::string& mobile_bridge_name);
    void set_mobile_tap_name(const std::string& mobile_tap_name);
    void set_wifi_tap_name(const std::string& wifi_tap_name);
    void set_ethernet_tap_name(const std::string& ethernet_tap_name);
    void set_session_id(uint32_t session_id);
    void set_use_allocd(bool use_allocd);
    void set_vsock_guest_cid(int vsock_guest_cid);
    void set_uuid(const std::string& uuid);
    // modem simulator related
    void set_modem_simulator_ports(const std::string& modem_simulator_ports);
    void set_virtual_disk_paths(const std::vector<std::string>& disk_paths);
    void set_webrtc_device_id(const std::string& id);
    void set_start_webrtc_signaling_server(bool start);
    void set_start_webrtc_sig_server_proxy(bool start);
    void set_start_wmediumd(bool start);
    void set_start_rootcanal(bool start);
    void set_start_ap(bool start);
    // Wifi MAC address inside the guest
    void set_wifi_mac_prefix(const int wifi_mac_prefix);
    // Gnss grpc proxy server port inside the host
    void set_gnss_grpc_proxy_server_port(int gnss_grpc_proxy_server_port);
    // Gnss grpc proxy local file path
    void set_gnss_file_path(const std::string &gnss_file_path);
  };

 private:
  std::unique_ptr<Json::Value> dictionary_;

  void SetPath(const std::string& key, const std::string& path);
  bool LoadFromFile(const char* file);
  static CuttlefishConfig* BuildConfigImpl(const std::string& path);

  CuttlefishConfig(const CuttlefishConfig&) = delete;
  CuttlefishConfig& operator=(const CuttlefishConfig&) = delete;
};

// Returns the instance number as obtained from the CUTTLEFISH_INSTANCE
// environment variable or the username.
int GetInstance();

// Returns default Vsock CID, which is
// GetInstance() + 2
int GetDefaultVsockCid();

// Calculates vsock server port number
// return base + (vsock_guest_cid - 3)
int GetVsockServerPort(const int base,
                       const int vsock_guest_cid);

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

std::string DefaultHostArtifactsPath(const std::string& file);
std::string HostBinaryPath(const std::string& file);
std::string DefaultGuestImagePath(const std::string& file);
std::string DefaultEnvironmentPath(const char* environment_key,
                                   const char* default_value,
                                   const char* path);

// Whether the host supports qemu
bool HostSupportsQemuCli();

// GPU modes
extern const char* const kGpuModeAuto;
extern const char* const kGpuModeGuestSwiftshader;
extern const char* const kGpuModeDrmVirgl;
extern const char* const kGpuModeGfxStream;

// HwComposer modes
extern const char* const kHwComposerAuto;
extern const char* const kHwComposerDrm;
extern const char* const kHwComposerRanchu;
}  // namespace cuttlefish
