/*
 * Copyright (C) 2017 The Android Open Source Project
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
#include "cuttlefish/host/commands/assemble_cvd/flags.h"

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_split.h"
#include "android-base/file.h"
#include "android-base/parseint.h"
#include "android-base/strings.h"
#include "fmt/format.h"
#include "fruit/fruit.h"
#include "gflags/gflags.h"
#include "json/json.h"
#include "json/writer.h"

#include "cuttlefish/common/libs/utils/base64.h"
#include "cuttlefish/common/libs/utils/container.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/common/libs/utils/known_paths.h"
#include "cuttlefish/common/libs/utils/network.h"
#include "cuttlefish/host/commands/assemble_cvd/alloc.h"
#include "cuttlefish/host/commands/assemble_cvd/assemble_cvd_flags.h"
#include "cuttlefish/host/commands/assemble_cvd/disk_image_flags_vectorization.h"
#include "cuttlefish/host/commands/assemble_cvd/display.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/android_efi_loader.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/blank_data_image_mb.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/boot_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/bootloader.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/cpus.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/daemon.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/data_policy.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/display_proto.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/extra_kernel_cmdline.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/gpu_mode.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/guest_enforce_security.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/initramfs_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/kernel_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/mcu_config_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/memory_mb.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/restart_subprocesses.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/use_cvdalloc.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/vendor_boot_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/vm_manager.h"
#include "cuttlefish/host/commands/assemble_cvd/graphics_flags.h"
#include "cuttlefish/host/commands/assemble_cvd/guest_config.h"
#include "cuttlefish/host/commands/assemble_cvd/network_flags.h"
#include "cuttlefish/host/commands/assemble_cvd/touchpad.h"
#include "cuttlefish/host/commands/cvdalloc/interface.h"
#include "cuttlefish/host/libs/config/ap_boot_flow.h"
#include "cuttlefish/host/libs/config/config_constants.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/display.h"
#include "cuttlefish/host/libs/config/fetcher_configs.h"
#include "cuttlefish/host/libs/config/gpu_mode.h"
#include "cuttlefish/host/libs/config/host_tools_version.h"
#include "cuttlefish/host/libs/config/instance_nums.h"
#include "cuttlefish/host/libs/config/secure_hals.h"
#include "cuttlefish/host/libs/config/vmm_mode.h"
#include "cuttlefish/host/libs/vhal_proxy_server/vhal_proxy_server_eth_addr.h"
#include "cuttlefish/host/libs/vm_manager/gem5_manager.h"
#include "cuttlefish/host/libs/vm_manager/qemu_manager.h"
#include "cuttlefish/host/libs/vm_manager/vm_manager.h"

#define GET_FLAG_STR_VALUE(name) GetFlagStrValueForInstances(FLAGS_ ##name, instances_size, #name, name_to_default_value)
#define GET_FLAG_INT_VALUE(name) GetFlagIntValueForInstances(FLAGS_ ##name, instances_size, #name, name_to_default_value)
#define GET_FLAG_BOOL_VALUE(name) GetFlagBoolValueForInstances(FLAGS_ ##name, instances_size, #name, name_to_default_value)

namespace cuttlefish {

using vm_manager::QemuManager;
using vm_manager::Gem5Manager;
using vm_manager::GetVmManager;

namespace {

Result<std::pair<uint16_t, uint16_t>> ParsePortRange(const std::string& flag) {
  static const std::regex rgx("[0-9]+:[0-9]+");
  CF_EXPECTF(std::regex_match(flag, rgx),
             "Port range flag has invalid value: '{}'", flag);
  std::pair<uint16_t, uint16_t> port_range;
  std::stringstream ss(flag);
  char c;
  ss >> port_range.first;
  ss.read(&c, 1);
  ss >> port_range.second;
  return port_range;
}

std::string StrForInstance(const std::string& prefix, int num) {
  std::ostringstream stream;
  stream << prefix << std::setfill('0') << std::setw(2) << num;
  return stream.str();
}

Result<std::unordered_map<int, std::string>> CreateNumToWebrtcDeviceIdMap(
    const CuttlefishConfig& tmp_config_obj,
    const std::vector<int32_t>& instance_nums,
    const std::string& webrtc_device_id_flag) {
  std::unordered_map<int, std::string> output_map;
  if (webrtc_device_id_flag.empty()) {
    for (const auto num : instance_nums) {
      const auto const_instance = tmp_config_obj.ForInstance(num);
      output_map[num] = const_instance.instance_name();
    }
    return output_map;
  }
  auto tokens = android::base::Tokenize(webrtc_device_id_flag, ",");
  CF_EXPECT(tokens.size() == 1 || tokens.size() == instance_nums.size(),
            "--webrtc_device_ids provided " << tokens.size()
                                            << " tokens"
                                               " while 1 or "
                                            << instance_nums.size()
                                            << " is expected.");
  CF_EXPECT(!tokens.empty(), "--webrtc_device_ids is ill-formatted");

  std::vector<std::string> device_ids;
  if (tokens.size() != instance_nums.size()) {
    /* this is only possible when tokens.size() == 1
     * and instance_nums.size() > 1. The token must include {num}
     * so that the token pattern can be expanded to multiple instances.
     */
    auto device_id = tokens.front();
    CF_EXPECT(device_id.find("{num}") != std::string::npos,
              "If one webrtc_device_ids is given for multiple instances, "
                  << " {num} should be included in webrtc_device_id.");
    device_ids = std::vector<std::string>(instance_nums.size(), tokens.front());
  }

  if (tokens.size() == instance_nums.size()) {
    // doesn't have to include {num}
    device_ids = std::move(tokens);
  }

  auto itr = device_ids.begin();
  for (const auto num : instance_nums) {
    std::string_view device_id_view(itr->data(), itr->size());
    output_map[num] = android::base::StringReplace(device_id_view, "{num}",
                                                   std::to_string(num), true);
    ++itr;
  }
  return output_map;
}

/**
 * Returns a mapping between flag name and "gflags default_value" as strings for flags
 * defined in the binary.
 */
std::map<std::string, std::string> CurrentFlagsToDefaultValue() {
  std::map<std::string, std::string> name_to_default_value;
  std::vector<gflags::CommandLineFlagInfo> self_flags;
  gflags::GetAllFlags(&self_flags);
  for (auto& flag : self_flags) {
    name_to_default_value[flag.name] = flag.default_value;
  }
  return name_to_default_value;
}

Result<std::vector<bool>> GetFlagBoolValueForInstances(
    const std::string& flag_values, int32_t instances_size,
    const std::string& flag_name,
    const std::map<std::string, std::string>& name_to_default_value) {
  std::vector<std::string_view> flag_vec = absl::StrSplit(flag_values, ",");
  std::vector<bool> value_vec(instances_size);

  auto default_value_it = name_to_default_value.find(flag_name);
  CF_EXPECT(default_value_it != name_to_default_value.end());
  std::vector<std::string_view> default_value_vec =
      absl::StrSplit(default_value_it->second, ",");

  for (int instance_index=0; instance_index<instances_size; instance_index++) {
    if (instance_index >= flag_vec.size()) {
      value_vec[instance_index] = value_vec[0];
    } else {
      if (flag_vec[instance_index] == "unset" || flag_vec[instance_index] == "\"unset\"") {
        std::string_view default_value = default_value_vec[0];
        if (instance_index < default_value_vec.size()) {
          default_value = default_value_vec[instance_index];
        }
        value_vec[instance_index] = CF_EXPECT(ParseBool(default_value, flag_name));
      } else {
        value_vec[instance_index] = CF_EXPECT(ParseBool(flag_vec[instance_index], flag_name));
      }
    }
  }
  return value_vec;
}

Result<std::vector<int>> GetFlagIntValueForInstances(
    const std::string& flag_values, int32_t instances_size,
    const std::string& flag_name,
    const std::map<std::string, std::string>& name_to_default_value) {
  std::vector<std::string> flag_vec = absl::StrSplit(flag_values, ",");
  std::vector<int> value_vec(instances_size);

  auto default_value_it = name_to_default_value.find(flag_name);
  CF_EXPECT(default_value_it != name_to_default_value.end());
  std::vector<std::string> default_value_vec =
      absl::StrSplit(default_value_it->second, ",");

  for (int instance_index=0; instance_index<instances_size; instance_index++) {
    if (instance_index >= flag_vec.size()) {
      value_vec[instance_index] = value_vec[0];
    } else {
      if (flag_vec[instance_index] == "unset" || flag_vec[instance_index] == "\"unset\"") {
        std::string default_value = default_value_vec[0];
        if (instance_index < default_value_vec.size()) {
          default_value = default_value_vec[instance_index];
        }
        CF_EXPECTF(
            android::base::ParseInt(default_value, &value_vec[instance_index]),
            "Failed to parse value '{}' for '{}'", default_value, flag_name);
      } else {
        CF_EXPECTF(android::base::ParseInt(flag_vec[instance_index],
                                           &value_vec[instance_index]),
                   "Failed to parse value '{}' for '{}'",
                   flag_vec[instance_index], flag_name);
      }
    }
  }
  return value_vec;
}

Result<std::vector<std::string>> GetFlagStrValueForInstances(
    const std::string& flag_values, int32_t instances_size,
    const std::string& flag_name,
    const std::map<std::string, std::string>& name_to_default_value) {
  std::vector<std::string_view> flag_vec = absl::StrSplit(flag_values, ",");
  std::vector<std::string> value_vec(instances_size);

  auto default_value_it = name_to_default_value.find(flag_name);
  CF_EXPECT(default_value_it != name_to_default_value.end());
  std::vector<std::string_view> default_value_vec =
      absl::StrSplit(default_value_it->second, ",");

  for (int instance_index=0; instance_index<instances_size; instance_index++) {
    if (instance_index >= flag_vec.size()) {
      value_vec[instance_index] = value_vec[0];
    } else {
      if (flag_vec[instance_index] == "unset" || flag_vec[instance_index] == "\"unset\"") {
        std::string_view default_value = default_value_vec[0];
        if (instance_index < default_value_vec.size()) {
          default_value = default_value_vec[instance_index];
        }
        value_vec[instance_index] = default_value;
      } else {
        value_vec[instance_index] = flag_vec[instance_index];
      }
    }
  }
  return value_vec;
}

Result<void> CheckSnapshotCompatible(
    const bool must_be_compatible,
    const std::map<int, GpuMode>& calculated_gpu_mode) {
  if (!must_be_compatible) {
    return {};
  }

  /*
   * TODO(kwstephenkim@): delete this block once virtio-fs is supported
   */
  CF_EXPECTF(
      gflags::GetCommandLineFlagInfoOrDie("enable_virtiofs").current_value ==
          "false",
      "--enable_virtiofs should be false for snapshot, consider \"{}\"",
      "--enable_virtiofs=false");

  /*
   * TODO(khei@): delete this block once usb is supported
   */
  CF_EXPECTF(gflags::GetCommandLineFlagInfoOrDie("enable_usb").current_value ==
                 "false",
             "--enable_usb should be false for snapshot, consider \"{}\"",
             "--enable_usb=false");

  /*
   * TODO(kwstephenkim@): delete this block once 3D gpu mode snapshots are
   * supported
   */
  for (const auto& [instance_index, instance_gpu_mode] : calculated_gpu_mode) {
    CF_EXPECTF(
        instance_gpu_mode == GpuMode::GuestSwiftshader,
        "Only 2D guest_swiftshader is supported for snapshot. Consider \"{}\"",
        "--gpu_mode=guest_swiftshader");
  }
  return {};
}

std::optional<std::string> EnvironmentUdsDir() {
  std::string environments_uds_dir =
      fmt::format("{}/cf_env_{}", TempDir(), getuid());
  if (DirectoryExists(environments_uds_dir) &&
      !CanAccess(environments_uds_dir, R_OK | W_OK | X_OK)) {
    return std::nullopt;
  }
  return environments_uds_dir;
}

std::optional<std::string> InstancesUdsDir() {
  std::string instances_uds_dir =
      fmt::format("{}/cf_avd_{}", TempDir(), getuid());
  if (DirectoryExists(instances_uds_dir) &&
      !CanAccess(instances_uds_dir, R_OK | W_OK | X_OK)) {
    return std::nullopt;
  }
  return instances_uds_dir;
}

} // namespace

Result<CuttlefishConfig> InitializeCuttlefishConfiguration(
    const std::string& root_dir, const std::vector<GuestConfig>& guest_configs,
    fruit::Injector<>& injector, const FetcherConfigs& fetcher_configs,
    const BootImageFlag& boot_image, const InitramfsPathFlag& initramfs_path,
    const KernelPathFlag& kernel_path, const SuperImageFlag& super_image,
    const SystemImageDirFlag& system_image_dir,
    const VendorBootImageFlag& vendor_boot_image,
    const VmManagerFlag& vm_manager_flag, const Defaults& defaults) {
  CuttlefishConfig tmp_config_obj;
  // If a snapshot path is provided, do not read all flags to set up the config.
  // Instead, read the config that was saved at time of snapshot and restore
  // that for this run.
  // TODO (khei@/kwstephenkim@): b/310034839
  const std::string snapshot_path = FLAGS_snapshot_path;
  if (!snapshot_path.empty()) {
    const std::string snapshot_path_config =
        snapshot_path + "/assembly/cuttlefish_config.json";
    tmp_config_obj.LoadFromFile(snapshot_path_config.c_str());
    tmp_config_obj.set_snapshot_path(snapshot_path);
    return tmp_config_obj;
  }

  for (const auto& fragment : injector.getMultibindings<ConfigFragment>()) {
    CF_EXPECTF(tmp_config_obj.SaveFragment(*fragment),
               "Failed to save fragment '{}'", fragment->Name());
  }

  tmp_config_obj.set_root_dir(root_dir);

  tmp_config_obj.set_environments_uds_dir(
      EnvironmentUdsDir().value_or(tmp_config_obj.environments_dir()));
  tmp_config_obj.set_instances_uds_dir(
      InstancesUdsDir().value_or(tmp_config_obj.instances_dir()));

  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());

  // TODO(weihsu), b/250988697: moved bootconfig_supported and hctr2_supported
  // into each instance, but target_arch is still in todo
  // target_arch should be in instance later
  std::unique_ptr<vm_manager::VmManager> vmm =
      GetVmManager(vm_manager_flag.Mode(), guest_configs[0].target_arch);
  tmp_config_obj.set_vm_manager(vm_manager_flag.Mode());
  tmp_config_obj.set_ap_vm_manager(ToString(vm_manager_flag.Mode()) +
                                   "_openwrt");

  // TODO: schuffelen - fix behavior on riscv64
  if (guest_configs[0].target_arch == Arch::RiscV64) {
    static constexpr char kRiscv64Secure[] = "keymint,gatekeeper,oemlock";
    SetCommandLineOptionWithMode("secure_hals", kRiscv64Secure,
                                 google::FlagSettingMode::SET_FLAGS_DEFAULT);
  } else {
    static constexpr char kDefaultSecure[] =
        "oemlock,guest_keymint_insecure,guest_gatekeeper_insecure";
    SetCommandLineOptionWithMode("secure_hals", kDefaultSecure,
                                 google::FlagSettingMode::SET_FLAGS_DEFAULT);
  }
  auto secure_hals = CF_EXPECT(ParseSecureHals(FLAGS_secure_hals));
  CF_EXPECT(ValidateSecureHals(secure_hals));
  tmp_config_obj.set_secure_hals(secure_hals);

  ExtraKernelCmdlineFlag extra_kernel_cmdline_value =
      ExtraKernelCmdlineFlag::FromGlobalGflags();
  tmp_config_obj.set_extra_kernel_cmdline(
      extra_kernel_cmdline_value.ForIndex(0));

  if (FLAGS_track_host_tools_crc) {
    tmp_config_obj.set_host_tools_version(HostToolsCrc());
  }

  tmp_config_obj.set_gem5_debug_flags(FLAGS_gem5_debug_flags);

  tmp_config_obj.set_sig_server_address(FLAGS_webrtc_sig_server_addr);

  tmp_config_obj.set_enable_metrics(FLAGS_report_anonymous_usage_stats);
  // TODO(moelsherif): Handle this flag (set_metrics_binary) in the future

  std::optional<bool> guest_config_mac80211_hwsim =
      guest_configs[0].enforce_mac80211_hwsim;
  if (guest_config_mac80211_hwsim.has_value()) {
    tmp_config_obj.set_virtio_mac80211_hwsim(*guest_config_mac80211_hwsim);
  } else {
    tmp_config_obj.set_virtio_mac80211_hwsim(true);
  }

  if ((FLAGS_ap_rootfs_image.empty()) != (FLAGS_ap_kernel_image.empty())) {
    LOG(FATAL) << "Either both ap_rootfs_image and ap_kernel_image should be "
        "set or neither should be set.";
  }
  // If user input multiple values, we only take the 1st value and shared with
  // all instances
  std::string ap_rootfs_image = "";
  if (!FLAGS_ap_rootfs_image.empty()) {
    ap_rootfs_image = android::base::Split(FLAGS_ap_rootfs_image, ",")[0];
  }

  tmp_config_obj.set_ap_rootfs_image(ap_rootfs_image);
  tmp_config_obj.set_ap_kernel_image(FLAGS_ap_kernel_image);

  tmp_config_obj.set_enable_host_nfc(FLAGS_enable_host_nfc);
  tmp_config_obj.set_enable_host_nfc_connector(FLAGS_enable_host_nfc);

  // get flag default values and store into map
  auto name_to_default_value = CurrentFlagsToDefaultValue();
  // old flags but vectorized for multi-device instances
  int32_t instances_size = instance_nums.size();

  // netsim flags allow all radios or selecting a specific radio
  std::vector<bool> netsim_all_radios_vec =
      CF_EXPECT(GET_FLAG_BOOL_VALUE(netsim));
  bool any_netsim_all_radios =
      std::any_of(netsim_all_radios_vec.begin(), netsim_all_radios_vec.end(),
                  [](bool e) { return e; });
  std::vector<bool> netsim_bt_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(netsim_bt));
  bool any_netsim_bt = std::any_of(netsim_bt_vec.begin(), netsim_bt_vec.end(),
                                   [](bool e) { return e; });
  std::vector<bool> netsim_uwb_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(netsim_uwb));
  bool any_netsim_uwb = std::any_of(
      netsim_uwb_vec.begin(), netsim_uwb_vec.end(), [](bool e) { return e; });
  bool netsim_has_bt = any_netsim_all_radios || any_netsim_bt;
  bool netsim_has_uwb = any_netsim_all_radios || any_netsim_uwb;

  // These flags inform NetsimServer::ResultSetup which radios it owns.
  if (netsim_has_bt) {
    tmp_config_obj.netsim_radio_enable(CuttlefishConfig::NetsimRadio::Bluetooth);
  }
  if (netsim_has_uwb) {
    tmp_config_obj.netsim_radio_enable(CuttlefishConfig::NetsimRadio::Uwb);
  }

  bool any_not_netsim_bt = false;
  bool any_not_netsim_uwb = false;
  for (int32_t i = 0; i < instances_size; ++i) {
    any_not_netsim_bt |= !netsim_all_radios_vec[i] && !netsim_bt_vec[i];
    any_not_netsim_uwb |= !netsim_all_radios_vec[i] && !netsim_uwb_vec[i];
  }

  std::vector<bool> enable_host_bluetooth_vec =
      CF_EXPECT(GET_FLAG_BOOL_VALUE(enable_host_bluetooth));

  // end of vectorize ap_rootfs_image, ap_kernel_image, wmediumd_config

  tmp_config_obj.set_enable_automotive_proxy(FLAGS_enable_automotive_proxy);

  std::vector<bool> enable_vhal_proxy_server_vec =
      CF_EXPECT(GET_FLAG_BOOL_VALUE(enable_vhal_proxy_server));

  std::vector<std::string> gnss_file_paths =
      CF_EXPECT(GET_FLAG_STR_VALUE(gnss_file_path));
  std::vector<std::string> fixed_location_file_paths =
      CF_EXPECT(GET_FLAG_STR_VALUE(fixed_location_file_path));
  std::vector<int> x_res_vec = CF_EXPECT(GET_FLAG_INT_VALUE(x_res));
  std::vector<int> y_res_vec = CF_EXPECT(GET_FLAG_INT_VALUE(y_res));
  std::vector<int> dpi_vec = CF_EXPECT(GET_FLAG_INT_VALUE(dpi));
  std::vector<int> refresh_rate_hz_vec = CF_EXPECT(GET_FLAG_INT_VALUE(
      refresh_rate_hz));
  std::vector<std::string> overlays_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(overlays));
  MemoryMbFlag memory_mb_values = CF_EXPECT(MemoryMbFlag::FromGlobalGflags());
  std::vector<int> camera_server_port_vec = CF_EXPECT(GET_FLAG_INT_VALUE(
      camera_server_port));
  std::vector<int> vsock_guest_cid_vec = CF_EXPECT(GET_FLAG_INT_VALUE(
      vsock_guest_cid));
  std::vector<std::string> vsock_guest_group_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(vsock_guest_group));
  CpusFlag cpus_values = CF_EXPECT(CpusFlag::FromGlobalGflags());
  BlankDataImageMbFlag blank_data_image_mb_values =
      CF_EXPECT(BlankDataImageMbFlag::FromGlobalGflags(guest_configs));
  std::vector<int> gdb_port_vec = CF_EXPECT(GET_FLAG_INT_VALUE(gdb_port));
  std::vector<std::string> setupwizard_mode_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(setupwizard_mode));
  std::vector<std::string> userdata_format_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(userdata_format));
  GuestEnforceSecurityFlag guest_enforce_security_values =
      CF_EXPECT(GuestEnforceSecurityFlag::FromGlobalGflags());
  std::vector<std::string> serial_number_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(serial_number));
  std::vector<bool> use_random_serial_vec =
      CF_EXPECT(GET_FLAG_BOOL_VALUE(use_random_serial));
  UseCvdallocFlag use_cvdalloc_values =
      CF_EXPECT(UseCvdallocFlag::FromGlobalGflags(defaults));
  std::vector<bool> use_sdcard_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(use_sdcard));
  std::vector<bool> pause_in_bootloader_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      pause_in_bootloader));
  std::vector<std::string> uuid_vec = CF_EXPECT(GET_FLAG_STR_VALUE(uuid));
  DaemonFlag daemon_values = CF_EXPECT(DaemonFlag::FromGlobalGflags());
  std::vector<bool> enable_minimal_mode_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      enable_minimal_mode));
  std::vector<bool> enable_modem_simulator_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      enable_modem_simulator));
  std::vector<int> modem_simulator_count_vec = CF_EXPECT(GET_FLAG_INT_VALUE(
      modem_simulator_count));
  std::vector<int> modem_simulator_sim_type_vec = CF_EXPECT(GET_FLAG_INT_VALUE(
      modem_simulator_sim_type));
  std::vector<bool> console_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(console));
  std::vector<bool> enable_audio_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(enable_audio));
  std::vector<bool> enable_usb_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(enable_usb));
  std::vector<bool> start_gnss_proxy_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      start_gnss_proxy));
  std::vector<bool> enable_bootanimation_vec =
      CF_EXPECT(GET_FLAG_BOOL_VALUE(enable_bootanimation));

  std::vector<std::string> extra_bootconfig_args_base64_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(extra_bootconfig_args_base64));

  std::vector<bool> record_screen_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      record_screen));
  std::vector<std::string> gem5_debug_file_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(gem5_debug_file));
  std::vector<bool> mte_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(mte));
  std::vector<bool> enable_kernel_log_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      enable_kernel_log));
  std::vector<bool> kgdb_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(kgdb));
  std::vector<std::string> boot_slot_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(boot_slot));
  std::vector<std::string> webrtc_assets_dir_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(webrtc_assets_dir));
  std::vector<std::string> tcp_port_range_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(tcp_port_range));
  std::vector<std::string> udp_port_range_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(udp_port_range));
  std::vector<bool> vhost_net_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      vhost_net));
  std::vector<std::string> vhost_user_vsock_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(vhost_user_vsock));
  std::vector<std::string> ril_dns_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(ril_dns));
  std::vector<bool> enable_jcard_simulator_vec =
      CF_EXPECT(GET_FLAG_BOOL_VALUE(enable_jcard_simulator));

  // At this time, FLAGS_enable_sandbox comes from SetDefaultFlagsForCrosvm
  std::vector<bool> enable_sandbox_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      enable_sandbox));
  std::vector<bool> enable_virtiofs_vec =
      CF_EXPECT(GET_FLAG_BOOL_VALUE(enable_virtiofs));

  GpuModeFlag gpu_mode_values = CF_EXPECT(GpuModeFlag::FromGlobalGflags());
  std::map<int, GpuMode> calculated_gpu_mode_vec;
  std::vector<std::string> gpu_vhost_user_mode_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(gpu_vhost_user_mode));
  std::vector<std::string> gpu_renderer_features_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(gpu_renderer_features));
  std::vector<std::string> gpu_context_types_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(gpu_context_types));
  std::vector<std::string> guest_hwui_renderer_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(guest_hwui_renderer));
  std::vector<std::string> guest_renderer_preload_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(guest_renderer_preload));
  std::vector<std::string> guest_vulkan_driver_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(guest_vulkan_driver));
  std::vector<std::string> frames_socket_path_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(frames_socket_path));

  std::vector<std::string> gpu_capture_binary_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(gpu_capture_binary));
  RestartSubprocessesFlag restart_subprocesses_values =
      CF_EXPECT(RestartSubprocessesFlag::FromGlobalGflags());
  std::vector<std::string> hwcomposer_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(hwcomposer));
  std::vector<bool> enable_gpu_udmabuf_vec =
      CF_EXPECT(GET_FLAG_BOOL_VALUE(enable_gpu_udmabuf));
  std::vector<bool> smt_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(smt));
  std::vector<std::string> crosvm_binary_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(crosvm_binary));
  std::vector<std::string> seccomp_policy_dir_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(seccomp_policy_dir));
  std::vector<std::string> qemu_binary_dir_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(qemu_binary_dir));

  // new instance specific flags (moved from common flags)
  std::vector<std::string> gem5_binary_dir_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(gem5_binary_dir));
  std::vector<std::string> gem5_checkpoint_dir_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(gem5_checkpoint_dir));
  DataPolicyFlag data_policy_values =
      CF_EXPECT(DataPolicyFlag::FromGlobalGflags());

  // multi-virtual-device multi-display proto input
  DisplaysProtoFlag instances_display_configs =
      CF_EXPECT(DisplaysProtoFlag::FromGlobalGflags());

  std::vector<bool> use_balloon_vec =
      CF_EXPECT(GET_FLAG_BOOL_VALUE(crosvm_use_balloon));
  std::vector<bool> use_rng_vec =
      CF_EXPECT(GET_FLAG_BOOL_VALUE(crosvm_use_rng));
  std::vector<bool> simple_media_device_vec =
      CF_EXPECT(GET_FLAG_BOOL_VALUE(crosvm_simple_media_device));
  std::vector<std::string> v4l2_proxy_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(crosvm_v4l2_proxy));
  std::vector<bool> use_pmem_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(use_pmem));
  std::vector<std::string> device_external_network_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(device_external_network));

  std::vector<bool> fail_fast_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(fail_fast));

  std::vector<bool> vhost_user_block_vec =
      CF_EXPECT(GET_FLAG_BOOL_VALUE(vhost_user_block));

  McuConfigPathFlag mcu_config_paths = McuConfigPathFlag::FromGlobalGflags();

  std::vector<std::string> vcpu_config_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(vcpu_config_path));

  std::vector<bool> enable_tap_devices_vec =
      CF_EXPECT(GET_FLAG_BOOL_VALUE(enable_tap_devices));

  std::string default_enable_sandbox = "";
  std::string default_enable_virtiofs = "";
  std::string comma_str = "";

  CF_EXPECT(FLAGS_use_overlay || instance_nums.size() == 1,
            "`--use_overlay=false` is incompatible with multiple instances");
  CF_EXPECT(!instance_nums.empty(), "Requires at least one instance.");

  auto rootcanal_instance_num = *instance_nums.begin() - 1;
  if (FLAGS_rootcanal_instance_num > 0) {
    rootcanal_instance_num = FLAGS_rootcanal_instance_num - 1;
  }
  tmp_config_obj.set_rootcanal_args(FLAGS_rootcanal_args);
  tmp_config_obj.set_rootcanal_hci_port(7300 + rootcanal_instance_num);
  tmp_config_obj.set_rootcanal_link_port(7400 + rootcanal_instance_num);
  tmp_config_obj.set_rootcanal_test_port(7500 + rootcanal_instance_num);
  tmp_config_obj.set_rootcanal_link_ble_port(7600 + rootcanal_instance_num);
  VLOG(0) << "rootcanal_instance_num: " << rootcanal_instance_num;
  VLOG(0) << "launch rootcanal: " << (FLAGS_rootcanal_instance_num <= 0);

  tmp_config_obj.set_casimir_args(FLAGS_casimir_args);
  auto casimir_instance_num = *instance_nums.begin() - 1;
  if (FLAGS_casimir_instance_num > 0) {
    casimir_instance_num = FLAGS_casimir_instance_num - 1;
  }
  tmp_config_obj.set_casimir_nci_port(7800 + casimir_instance_num);
  tmp_config_obj.set_casimir_rf_port(7900 + casimir_instance_num);
  VLOG(0) << "casimir_instance_num: " << casimir_instance_num;
  VLOG(0) << "launch casimir: " << (FLAGS_casimir_instance_num <= 0);

  int netsim_instance_num = *instance_nums.begin() - 1;
  tmp_config_obj.set_netsim_instance_num(netsim_instance_num);
  VLOG(0) << "netsim_instance_num: " << netsim_instance_num;
  tmp_config_obj.set_netsim_args(FLAGS_netsim_args);
  // netsim built-in connector will forward packets to another daemon instance,
  // filling the role of bluetooth_connector when is_bt_netsim is true.
  auto netsim_connector_instance_num = netsim_instance_num;
  if (netsim_instance_num != rootcanal_instance_num) {
    netsim_connector_instance_num = rootcanal_instance_num;
  }
  tmp_config_obj.set_netsim_connector_instance_num(
      netsim_connector_instance_num);

  // crosvm should create fifos for UWB
  auto pica_instance_num = *instance_nums.begin() - 1;
  if (FLAGS_pica_instance_num > 0) {
    pica_instance_num = FLAGS_pica_instance_num - 1;
  }
  tmp_config_obj.set_enable_host_uwb(FLAGS_enable_host_uwb || any_netsim_uwb);

  tmp_config_obj.set_pica_uci_port(7000 + pica_instance_num);
  VLOG(0) << "launch pica: " << (FLAGS_pica_instance_num <= 0);

  auto straced = android::base::Tokenize(FLAGS_straced_host_executables, ",");
  std::set<std::string> straced_set(straced.begin(), straced.end());
  tmp_config_obj.set_straced_host_executables(straced_set);

  tmp_config_obj.set_kvm_path(FLAGS_kvm_path);
  tmp_config_obj.set_vhost_vsock_path(FLAGS_vhost_vsock_path);

  // Environment specific configs
  // Currently just setting for the default environment
  auto environment_name =
      std::string("env-") + std::to_string(instance_nums[0]);
  auto mutable_env_config = tmp_config_obj.ForEnvironment(environment_name);
  auto env_config = const_cast<const CuttlefishConfig&>(tmp_config_obj)
                        .ForEnvironment(environment_name);

  mutable_env_config.set_group_uuid(std::time(0));

  std::vector<bool> enable_wifi_vec =
      CF_EXPECT(GET_FLAG_BOOL_VALUE(enable_wifi));
  bool enable_wifi = std::any_of(enable_wifi_vec.begin(), enable_wifi_vec.end(),
                                 [](bool e) { return e; });
  mutable_env_config.set_enable_wifi(enable_wifi);

  mutable_env_config.set_vhost_user_mac80211_hwsim(
      FLAGS_vhost_user_mac80211_hwsim);

  mutable_env_config.set_wmediumd_config(FLAGS_wmediumd_config);

  // Start wmediumd process for the first instance if
  // vhost_user_mac80211_hwsim is not specified.
  const bool start_wmediumd = tmp_config_obj.virtio_mac80211_hwsim() &&
                              FLAGS_vhost_user_mac80211_hwsim.empty() &&
                              enable_wifi;
  if (start_wmediumd) {
    auto vhost_user_socket_path =
        env_config.PerEnvironmentUdsPath("vhost_user_mac80211");
    auto wmediumd_api_socket_path =
        env_config.PerEnvironmentUdsPath("wmediumd_api_server");

    if (!instance_nums.empty()) {
      mutable_env_config.set_wmediumd_mac_prefix(5554);
    }
    mutable_env_config.set_vhost_user_mac80211_hwsim(vhost_user_socket_path);
    mutable_env_config.set_wmediumd_api_server_socket(wmediumd_api_socket_path);

    mutable_env_config.set_start_wmediumd(true);
  } else {
    mutable_env_config.set_start_wmediumd(false);
  }

  const auto graphics_availability =
      GetGraphicsAvailabilityWithSubprocessCheck();

  // Instance specific configs
  bool is_first_instance = true;
  int instance_index = 0;
  auto num_to_webrtc_device_id_flag_map =
      CF_EXPECT(CreateNumToWebrtcDeviceIdMap(tmp_config_obj, instance_nums,
                                             FLAGS_webrtc_device_id));
  size_t provided_serials_cnt =
      android::base::Split(FLAGS_serial_number, ",").size();
  CF_EXPECTF(
      provided_serials_cnt == 1 || provided_serials_cnt == instances_size,
      "Must have a single serial number prefix or one serial number per "
      "instance, have {} but expectected {}",
      provided_serials_cnt, instances_size);
  if (provided_serials_cnt == 1 && instances_size > 1) {
    // Make sure the serial numbers are different when running multiple
    // instances and using the default value for the flag
    for (size_t i = 0; i < instance_nums.size(); ++i) {
      serial_number_vec[i] += std::to_string(instance_nums[i]);
    }
  }
  for (const auto& num : instance_nums) {
    auto instance = tmp_config_obj.ForInstance(num);
    auto const_instance =
        const_cast<const CuttlefishConfig&>(tmp_config_obj).ForInstance(num);

    instance.set_use_cvdalloc(use_cvdalloc_values.ForIndex(instance_index));

    IfaceConfig iface_config =
        CF_EXPECT(DefaultNetworkInterfaces(const_instance));

    instance.set_crosvm_use_balloon(use_balloon_vec[instance_index]);
    instance.set_crosvm_use_rng(use_rng_vec[instance_index]);
    instance.set_crosvm_simple_media_device(simple_media_device_vec[instance_index]);
    instance.set_crosvm_v4l2_proxy(v4l2_proxy_vec[instance_index]);
    instance.set_use_pmem(use_pmem_vec[instance_index]);
    instance.set_bootconfig_supported(guest_configs[instance_index].bootconfig_supported);
    instance.set_enable_mouse(guest_configs[instance_index].mouse_supported);
    instance.set_enable_gamepad(
        guest_configs[instance_index].gamepad_supported);
    if (guest_configs[instance_index].custom_keyboard_config.has_value()) {
      instance.set_custom_keyboard_config(
          guest_configs[instance_index].custom_keyboard_config.value());
    }
    if (guest_configs[instance_index].domkey_mapping_config.has_value()) {
      instance.set_domkey_mapping_config(
          guest_configs[instance_index].domkey_mapping_config.value());
    }
    instance.set_filename_encryption_mode(
      guest_configs[instance_index].hctr2_supported ? "hctr2" : "cts");
    instance.set_enable_audio(enable_audio_vec[instance_index]);
    instance.set_enable_usb(enable_usb_vec[instance_index]);
    instance.set_enable_gnss_grpc_proxy(start_gnss_proxy_vec[instance_index]);
    instance.set_enable_bootanimation(enable_bootanimation_vec[instance_index]);

    instance.set_extra_bootconfig_args(FLAGS_extra_bootconfig_args);
    if (!extra_bootconfig_args_base64_vec[instance_index].empty()) {
      std::vector<uint8_t> decoded_args;
      CF_EXPECT(DecodeBase64(extra_bootconfig_args_base64_vec[instance_index],
                             &decoded_args));
      std::string decoded_args_str(decoded_args.begin(), decoded_args.end());
      instance.set_extra_bootconfig_args(decoded_args_str);
    }

    instance.set_record_screen(record_screen_vec[instance_index]);
    instance.set_gem5_debug_file(gem5_debug_file_vec[instance_index]);
    instance.set_mte(mte_vec[instance_index]);
    instance.set_enable_kernel_log(enable_kernel_log_vec[instance_index]);
    if (!boot_slot_vec[instance_index].empty()) {
      instance.set_boot_slot(boot_slot_vec[instance_index]);
    }

    instance.set_crosvm_binary(crosvm_binary_vec[instance_index]);
    instance.set_seccomp_policy_dir(seccomp_policy_dir_vec[instance_index]);
    instance.set_qemu_binary_dir(qemu_binary_dir_vec[instance_index]);

    // wifi, bluetooth, Thread, connectivity setup

    instance.set_vhost_net(vhost_net_vec[instance_index]);
    instance.set_openthread_node_id(num);

    // end of wifi, bluetooth, Thread, connectivity setup

    instance.set_audio_output_streams_count(
        guest_configs[instance_index].output_audio_streams_count);

    // jcardsim
    instance.set_enable_jcard_simulator(
        enable_jcard_simulator_vec[instance_index]);

    if (enable_jcard_simulator_vec[instance_index]) {
      const auto& secure_hals = CF_EXPECT(tmp_config_obj.secure_hals());
      if (0 == secure_hals.count(SecureHal::kGuestStrongboxInsecure)) {
        // When the enable_jcard_simulator flag is enabled, include the keymint
        // and secure_element hals, which interact with jcard simulator.
        static constexpr char kDefaultSecure[] =
            "oemlock,guest_keymint_insecure,guest_gatekeeper_insecure,guest_"
            "strongbox_insecure";

        auto secure_hals = CF_EXPECT(ParseSecureHals(kDefaultSecure));
        CF_EXPECT(ValidateSecureHals(secure_hals));
        tmp_config_obj.set_secure_hals(secure_hals);
      }
    }

    if (vhost_user_vsock_vec[instance_index] == kVhostUserVsockModeAuto) {
      std::set<Arch> default_on_arch = {Arch::Arm64};
      if (guest_configs[instance_index].vhost_user_vsock) {
        instance.set_vhost_user_vsock(true);
      } else if (VmManagerIsCrosvm(tmp_config_obj) &&
                 default_on_arch.find(
                     guest_configs[instance_index].target_arch) !=
                     default_on_arch.end()) {
        instance.set_vhost_user_vsock(true);
      } else {
        instance.set_vhost_user_vsock(false);
      }
    } else if (vhost_user_vsock_vec[instance_index] ==
               kVhostUserVsockModeTrue) {
      CF_EXPECT_EQ(tmp_config_obj.vm_manager(), VmmMode::kCrosvm,
                   "For now, only crosvm supports vhost_user_vsock");
      instance.set_vhost_user_vsock(true);
    } else if (vhost_user_vsock_vec[instance_index] ==
               kVhostUserVsockModeFalse) {
      instance.set_vhost_user_vsock(false);
    } else {
      return CF_ERRF(
          "--vhost_user_vsock should be one of 'auto', 'true', 'false', but "
          "got '{}'",
          vhost_user_vsock_vec[instance_index]);
    }

    if (use_random_serial_vec[instance_index]) {
      instance.set_serial_number(
          RandomSerialNumber("CFCVD" + std::to_string(num)));
    } else {
      instance.set_serial_number(serial_number_vec[instance_index]);
    }

    instance.set_grpc_socket_path(const_instance.PerInstanceGrpcSocketPath(""));

    // call this before all stuff that has vsock server: e.g. touchpad, keyboard, etc
    const auto vsock_guest_cid = vsock_guest_cid_vec[instance_index] + num - GetInstance();
    instance.set_vsock_guest_cid(vsock_guest_cid);
    auto calc_vsock_port = [vsock_guest_cid](const int base_port) {
      // a base (vsock) port is like 9600 for modem_simulator, etc
      return cuttlefish::GetVsockServerPort(base_port, vsock_guest_cid);
    };

    const auto vsock_guest_group = vsock_guest_group_vec[instance_index];
    instance.set_vsock_guest_group(vsock_guest_group);

    instance.set_session_id(iface_config.mobile_tap.session_id);

    instance.set_cpus(cpus_values.ForIndex(instance_index));
    // make sure all instances have multiple of 2 then SMT mode
    // if any of instance doesn't have multiple of 2 then NOT SMT
    CF_EXPECT(!smt_vec[instance_index] ||
                  cpus_values.ForIndex(instance_index) % 2 == 0,
              "CPUs must be a multiple of 2 in SMT mode");
    instance.set_smt(smt_vec[instance_index]);

    // new instance specific flags (moved from common flags)
    CF_EXPECT(instance_index < guest_configs.size(),
              "instance_index " << instance_index << " out of boundary "
                                << guest_configs.size());
    instance.set_target_arch(guest_configs[instance_index].target_arch);
    instance.set_device_type(guest_configs[instance_index].device_type);
    instance.set_guest_android_version(
        guest_configs[instance_index].android_version_number);
    instance.set_console(console_vec[instance_index]);
    instance.set_kgdb(console_vec[instance_index] && kgdb_vec[instance_index]);
    instance.set_blank_data_image_mb(
        blank_data_image_mb_values.ForIndex(instance_index));
    instance.set_gdb_port(gdb_port_vec[instance_index]);
    instance.set_fail_fast(fail_fast_vec[instance_index]);
    if (vhost_user_block_vec[instance_index]) {
      CF_EXPECT_EQ(tmp_config_obj.vm_manager(), VmmMode::kCrosvm,
                   "vhost-user block only supported on crosvm");
    }
    instance.set_vhost_user_block(vhost_user_block_vec[instance_index]);

    std::optional<std::vector<CuttlefishConfig::DisplayConfig>>
        binding_displays_configs;
    auto displays_configs_bindings =
        injector.getMultibindings<DisplaysConfigs>();
    CF_EXPECT_EQ(displays_configs_bindings.size(), 1,
                 "Expected a single binding?");
    if (auto configs = displays_configs_bindings[0]->GetConfigs();
        !configs.empty()) {
      binding_displays_configs = configs;
    }

    std::vector<CuttlefishConfig::DisplayConfig> display_configs;
    // assume displays proto input has higher priority than original display inputs
    if (instances_display_configs.Config()) {
      if (instance_index < instances_display_configs.Config()->size()) {
        display_configs = (*instances_display_configs.Config())[instance_index];
      }  // else display_configs is an empty vector
    } else if (binding_displays_configs) {
      display_configs = *binding_displays_configs;
    }

    if (x_res_vec[instance_index] > 0 && y_res_vec[instance_index] > 0) {
      if (display_configs.empty()) {
        display_configs.push_back({
            .width = x_res_vec[instance_index],
            .height = y_res_vec[instance_index],
            .dpi = dpi_vec[instance_index],
            .refresh_rate_hz = refresh_rate_hz_vec[instance_index],
            .overlays = overlays_vec[instance_index],
        });
      } else {
        LOG(WARNING)
            << "Ignoring --x_res and --y_res when --display specified.";
      }
    }
    instance.set_display_configs(display_configs);

    auto touchpad_configs_bindings =
        injector.getMultibindings<TouchpadsConfigs>();
    CF_EXPECT_EQ(touchpad_configs_bindings.size(), 1,
                 "Expected a single binding?");
    auto touchpad_configs = touchpad_configs_bindings[0]->GetConfigs();
    instance.set_touchpad_configs(touchpad_configs);

    instance.set_memory_mb(memory_mb_values.ForIndex(instance_index));
    instance.set_ddr_mem_mb(memory_mb_values.ForIndex(instance_index) * 1.2);
    CF_EXPECT(
        instance.set_setupwizard_mode(setupwizard_mode_vec[instance_index]));
    instance.set_userdata_format(userdata_format_vec[instance_index]);
    instance.set_guest_enforce_security(
        guest_enforce_security_values.ForIndex(instance_index));
    instance.set_pause_in_bootloader(pause_in_bootloader_vec[instance_index]);
    instance.set_run_as_daemon(daemon_values.ForIndex(instance_index));
    instance.set_enable_modem_simulator(enable_modem_simulator_vec[instance_index] &&
                                        !enable_minimal_mode_vec[instance_index]);
    instance.set_modem_simulator_instance_number(modem_simulator_count_vec[instance_index]);
    instance.set_modem_simulator_sim_type(modem_simulator_sim_type_vec[instance_index]);

    instance.set_enable_minimal_mode(enable_minimal_mode_vec[instance_index]);
    instance.set_camera_server_port(camera_server_port_vec[instance_index]);
    instance.set_gem5_binary_dir(gem5_binary_dir_vec[instance_index]);
    instance.set_gem5_checkpoint_dir(gem5_checkpoint_dir_vec[instance_index]);
    instance.set_data_policy(data_policy_values.ForIndex(instance_index));

    instance.set_has_wifi_card(enable_wifi_vec[instance_index]);
    instance.set_mobile_bridge_name(StrForInstance("cvd-mbr-", num));
    if (const_instance.use_cvdalloc()) {
      instance.set_wifi_bridge_name(std::string(kCvdallocWirelessBridgeName));
      instance.set_ethernet_bridge_name(
          std::string(kCvdallocEthernetBridgeName));
    } else {
      instance.set_wifi_bridge_name("cvd-wbr");
      instance.set_ethernet_bridge_name("cvd-ebr");
    }
    instance.set_mobile_tap_name(iface_config.mobile_tap.name);

    CF_EXPECT(ConfigureNetworkSettings(ril_dns_vec[instance_index],
                                       const_instance, instance));

    bool use_non_bridged_wireless =
        (NetworkInterfaceExists(iface_config.non_bridged_wireless_tap.name) ||
         const_instance.use_cvdalloc()) &&
        tmp_config_obj.virtio_mac80211_hwsim();
    if (use_non_bridged_wireless) {
      instance.set_use_bridged_wifi_tap(false);
      instance.set_wifi_tap_name(iface_config.non_bridged_wireless_tap.name);
    } else {
      instance.set_use_bridged_wifi_tap(true);
      instance.set_wifi_tap_name(iface_config.bridged_wireless_tap.name);
    }

    instance.set_ethernet_tap_name(iface_config.ethernet_tap.name);

    // crosvm should create fifos for Bluetooth
    bool enable_host_bluetooth = enable_host_bluetooth_vec[instance_index];
    bool is_netsim_all = netsim_all_radios_vec[instance_index];
    bool is_bt_netsim = is_netsim_all || netsim_bt_vec[instance_index];
    // or is_bt_netsim is here for backwards compatibility only
    instance.set_has_bluetooth(enable_host_bluetooth || is_bt_netsim);
    // rootcanal and bt_connector should handle Bluetooth (instead of netsim)
    instance.set_enable_host_bluetooth_connector(enable_host_bluetooth &&
                                                 !is_bt_netsim);

    bool is_uwb_netsim = is_netsim_all || netsim_uwb_vec[instance_index];
    // netsim has its own connector for uwb
    instance.set_enable_host_uwb_connector(FLAGS_enable_host_uwb &&
                                           !is_uwb_netsim);

    bool is_any_netsim = is_netsim_all || is_bt_netsim || is_uwb_netsim;

    instance.set_uuid(uuid_vec[instance_index]);

    instance.set_environment_name(environment_name);

    instance.set_modem_simulator_host_id(1000 + num);  // Must be 4 digits
    // the deprecated vnc was 6444 + num - 1, and qemu_vnc was vnc - 5900
    instance.set_qemu_vnc_server_port(544 + num - 1);
    instance.set_adb_host_port(6520 + num - 1);
    instance.set_adb_ip_and_port("0.0.0.0:" + std::to_string(6520 + num - 1));
    instance.set_fastboot_host_port(const_instance.adb_host_port());

    instance.set_enable_vhal_proxy_server(
        enable_vhal_proxy_server_vec[instance_index]);
    instance.set_vhal_proxy_server_port(
        cuttlefish::vhal_proxy_server::kDefaultEthPort + num - 1);

    uint8_t ethernet_mac[6] = {};
    uint8_t mobile_mac[6] = {};
    uint8_t wifi_mac[6] = {};
    uint8_t ethernet_ipv6[16] = {};
    GenerateEthMacForInstance(num - 1, ethernet_mac);
    GenerateMobileMacForInstance(num - 1, mobile_mac);
    GenerateWifiMacForInstance(num - 1, wifi_mac);
    GenerateCorrespondingIpv6ForMac(ethernet_mac, ethernet_ipv6);

    instance.set_ethernet_mac(MacAddressToString(ethernet_mac));
    instance.set_mobile_mac(MacAddressToString(mobile_mac));
    instance.set_wifi_mac(MacAddressToString(wifi_mac));
    instance.set_ethernet_ipv6(Ipv6ToString(ethernet_ipv6));

    instance.set_tombstone_receiver_port(calc_vsock_port(6600));
    instance.set_audiocontrol_server_port(
        9410); /* OK to use the same port number across instances */
    instance.set_lights_server_port(calc_vsock_port(6900));

    // gpu related settings
    const GpuMode gpu_mode = CF_EXPECT(ConfigureGpuSettings(
        graphics_availability, gpu_mode_values.ForIndex(instance_index),
        gpu_vhost_user_mode_vec[instance_index],
        gpu_renderer_features_vec[instance_index],
        gpu_context_types_vec[instance_index],
        guest_hwui_renderer_vec[instance_index],
        guest_renderer_preload_vec[instance_index], vm_manager_flag.Mode(),
        guest_configs[instance_index], instance));
    calculated_gpu_mode_vec[instance_index] =
        gpu_mode_values.ForIndex(instance_index);

    instance.set_restart_subprocesses(
        restart_subprocesses_values.ForIndex(instance_index));
    instance.set_gpu_capture_binary(gpu_capture_binary_vec[instance_index]);
    if (!gpu_capture_binary_vec[instance_index].empty()) {
      CF_EXPECT(gpu_mode == GpuMode::Gfxstream ||
                    gpu_mode == GpuMode::GfxstreamGuestAngle,
                "GPU capture only supported with --gpu_mode=gfxstream");

      // GPU capture runs in a detached mode where the "launcher" process
      // intentionally exits immediately.
      CF_EXPECT(!restart_subprocesses_values.ForIndex(instance_index),
                "GPU capture only supported with --norestart_subprocesses");
    }

    instance.set_hwcomposer(hwcomposer_vec[instance_index]);
    if (!hwcomposer_vec[instance_index].empty()) {
      if (hwcomposer_vec[instance_index] == kHwComposerRanchu) {
        CF_EXPECT(gpu_mode != GpuMode::DrmVirgl,
                  "ranchu hwcomposer not supported with --gpu_mode=drm_virgl");
      }
    }

    if (hwcomposer_vec[instance_index] == kHwComposerAuto) {
      if (gpu_mode == GpuMode::DrmVirgl) {
        instance.set_hwcomposer(kHwComposerDrm);
      } else if (gpu_mode == GpuMode::None) {
        instance.set_hwcomposer(kHwComposerNone);
      } else {
        instance.set_hwcomposer(kHwComposerRanchu);
      }
    }

    instance.set_enable_gpu_udmabuf(enable_gpu_udmabuf_vec[instance_index]);

    instance.set_gpu_context_types(gpu_context_types_vec[instance_index]);
    instance.set_guest_vulkan_driver(guest_vulkan_driver_vec[instance_index]);

    instance.set_guest_uses_bgra_framebuffers(
        guest_configs[instance_index].supports_bgra_framebuffers);

    if (!frames_socket_path_vec[instance_index].empty()) {
      instance.set_frames_socket_path(frames_socket_path_vec[instance_index]);
    } else {
      instance.set_frames_socket_path(
          const_instance.PerInstanceInternalUdsPath("frames.sock"));
    }

    // 1. Keep original code order SetCommandLineOptionWithMode("enable_sandbox")
    // then set_enable_sandbox later.
    // 2. SetCommandLineOptionWithMode condition: if gpu_mode or console,
    // then SetCommandLineOptionWithMode false as original code did,
    // otherwise keep default enable_sandbox value.
    // 3. Sepolicy rules need to be updated to support gpu mode. Temporarily disable
    // auto-enabling sandbox when gpu is enabled (b/152323505).
    default_enable_sandbox += comma_str;
    default_enable_virtiofs += comma_str;
    if (gpu_mode != GpuMode::GuestSwiftshader) {
      // original code, just moved to each instance setting block
      default_enable_sandbox += "false";
      default_enable_virtiofs += "false";
    } else {
      default_enable_sandbox += fmt::format(
          "{}", static_cast<bool>(enable_sandbox_vec[instance_index]));
      default_enable_virtiofs += fmt::format(
          "{}", static_cast<bool>(enable_virtiofs_vec[instance_index]));
    }
    comma_str = ",";

    CF_EXPECT(vmm->ConfigureGraphics(const_instance));

    // end of gpu related settings

    instance.set_gnss_grpc_proxy_server_port(7200 + num -1);
    instance.set_gnss_file_path(gnss_file_paths[instance_index]);
    instance.set_fixed_location_file_path(fixed_location_file_paths[instance_index]);

    std::vector<std::string> virtual_disk_paths;

    // Gem5 already uses CoW wrappers around disk images
    if (FLAGS_use_overlay && !VmManagerIsGem5(vm_manager_flag)) {
      auto path = const_instance.PerInstancePath("overlay.img");
      virtual_disk_paths.push_back(path);
    } else {
      virtual_disk_paths.push_back(const_instance.os_composite_disk_path());
    }

    bool persistent_disk = !VmManagerIsGem5(vm_manager_flag);
    if (persistent_disk) {
#ifdef __APPLE__
      const std::string persistent_composite_img_base =
          "persistent_composite.img";
#else
      const std::string persistent_composite_img_base =
          VmManagerIsQemu(tmp_config_obj) ? "persistent_composite_overlay.img"
                                          : "persistent_composite.img";
#endif
      auto path =
          const_instance.PerInstancePath(persistent_composite_img_base.data());
      virtual_disk_paths.push_back(path);
    }

    instance.set_use_sdcard(use_sdcard_vec[instance_index]);

    bool sdcard = use_sdcard_vec[instance_index];
    if (sdcard) {
      if (VmManagerIsQemu(tmp_config_obj)) {
        virtual_disk_paths.push_back(const_instance.sdcard_overlay_path());
      } else {
        virtual_disk_paths.push_back(const_instance.sdcard_path());
      }
    }

    instance.set_virtual_disk_paths(virtual_disk_paths);

    // We'd like to set mac prefix to be 5554, 5555, 5556, ... in normal cases.
    // When --base_instance_num=3, this might be 5556, 5557, 5558, ... (skipping
    // first two)
    instance.set_wifi_mac_prefix(5554 + (num - 1));

    // streaming, webrtc setup
    instance.set_webrtc_assets_dir(webrtc_assets_dir_vec[instance_index]);

    std::pair<uint16_t, uint16_t> tcp_range =
        CF_EXPECT(ParsePortRange(tcp_port_range_vec[instance_index]));
    instance.set_webrtc_tcp_port_range(tcp_range);

    std::pair<uint16_t, uint16_t> udp_range =
        CF_EXPECT(ParsePortRange(udp_port_range_vec[instance_index]));
    instance.set_webrtc_udp_port_range(udp_range);

    // end of streaming, webrtc setup

    CF_EXPECT(Contains(num_to_webrtc_device_id_flag_map, num),
              "Error in looking up num to webrtc_device_id_flag_map");
    instance.set_webrtc_device_id(num_to_webrtc_device_id_flag_map[num]);

    auto port = 8443 + num - 1;
    // Change the signaling server port for all instances
    tmp_config_obj.set_sig_server_proxy_port(port);
    instance.set_start_netsim(is_first_instance && is_any_netsim);

    instance.set_start_rootcanal(is_first_instance && any_not_netsim_bt &&
                                 (FLAGS_rootcanal_instance_num <= 0));

    instance.set_start_casimir(is_first_instance &&
                               FLAGS_casimir_instance_num <= 0);

    instance.set_start_pica(is_first_instance && any_not_netsim_uwb &&
                            FLAGS_pica_instance_num <= 0);

    // TODO(b/288987294) Remove this when separating environment is done
    bool instance_start_wmediumd = is_first_instance && start_wmediumd;
    instance.set_start_wmediumd_instance(instance_start_wmediumd);

    if (!FLAGS_ap_rootfs_image.empty() && !FLAGS_ap_kernel_image.empty() &&
        const_instance.start_wmediumd_instance()) {
      // TODO(264537774): Ubuntu grub modules / grub monoliths cannot be used to
      // boot 64 bit kernel using 32 bit u-boot / grub. Enable this code back
      // after making sure it works across all popular environments if
      // (CanGenerateEsp(guest_configs[0].target_arch)) {
      //   instance.set_ap_boot_flow(APBootFlow::Grub);
      // } else {
      //   instance.set_ap_boot_flow(APBootFlow::LegacyDirect);
      // }
      instance.set_ap_boot_flow(APBootFlow::LegacyDirect);
    } else {
      instance.set_ap_boot_flow(APBootFlow::None);
    }

    is_first_instance = false;

    // instance.modem_simulator_ports := "" or "[port,]*port"
    if (modem_simulator_count_vec[instance_index] > 0) {
      std::stringstream modem_ports;
      for (auto index {0}; index < modem_simulator_count_vec[instance_index] - 1; index++) {
        auto port = 9600 + (modem_simulator_count_vec[instance_index] * (num - 1)) + index;
        modem_ports << calc_vsock_port(port) << ",";
      }
      auto port = 9600 + (modem_simulator_count_vec[instance_index] * (num - 1)) +
                  modem_simulator_count_vec[instance_index] - 1;
      modem_ports << calc_vsock_port(port);
      instance.set_modem_simulator_ports(modem_ports.str());
    } else {
      instance.set_modem_simulator_ports("");
    }

    auto external_network_mode = CF_EXPECT(
        ParseExternalNetworkMode(device_external_network_vec[instance_index]));
    CF_EXPECT(external_network_mode == ExternalNetworkMode::kTap ||
                  VmManagerIsQemu(vm_manager_flag),
              "TODO(b/286284441): slirp only works on QEMU");
    instance.set_external_network_mode(external_network_mode);

    instance.set_mcu(CF_EXPECT(mcu_config_paths.JsonForIndex(instance_index)));

    if (!vcpu_config_vec[instance_index].empty()) {
      auto vcpu_cfg_path = vcpu_config_vec[instance_index];
      CF_EXPECT(FileExists(vcpu_cfg_path), "vCPU config file does not exist");
      instance.set_vcpu_config_path(AbsolutePath(vcpu_cfg_path));
    }

    if (!guest_configs[instance_index].ti50_emulator.empty()) {
      auto ti50_emulator =
          DefaultHostArtifactsPath(guest_configs[instance_index].ti50_emulator);
      CF_EXPECT(FileExists(ti50_emulator),
                "ti50 emulator binary does not exist");
      instance.set_ti50_emulator(ti50_emulator);
    }

    instance.set_enable_tap_devices(enable_tap_devices_vec[instance_index]);

    instance_index++;
  }  // end of num_instances loop

  std::vector<std::string> names;
  names.reserve(tmp_config_obj.Instances().size());
  for (const auto& instance : tmp_config_obj.Instances()) {
    names.emplace_back(instance.instance_name());
  }
  tmp_config_obj.set_instance_names(names);

  // keep legacy values for acloud or other related tools (b/262284453)
  tmp_config_obj.set_crosvm_binary(crosvm_binary_vec[0]);

  // Keep the original code here to set enable_sandbox commandline flag value
  SetCommandLineOptionWithMode("enable_sandbox", default_enable_sandbox.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);

  // Set virtiofs to match enable_sandbox as it did before adding
  // enable_virtiofs flag.
  SetCommandLineOptionWithMode("enable_virtiofs",
                               default_enable_sandbox.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);

  // After SetCommandLineOptionWithMode,
  // default flag values changed, need recalculate name_to_default_value
  name_to_default_value = CurrentFlagsToDefaultValue();
  // After last SetCommandLineOptionWithMode, we could set these special flags
  enable_sandbox_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      enable_sandbox));
  enable_virtiofs_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(enable_virtiofs));

  instance_index = 0;
  for (const auto& num : instance_nums) {
    auto instance = tmp_config_obj.ForInstance(num);
    instance.set_enable_sandbox(enable_sandbox_vec[instance_index]);
    instance.set_enable_virtiofs(enable_virtiofs_vec[instance_index]);
    instance_index++;
  }

  const auto& environment_specific =
      (static_cast<const CuttlefishConfig&>(tmp_config_obj))
          .ForEnvironment(environment_name);
  CF_EXPECT(CheckSnapshotCompatible(FLAGS_snapshot_compatible &&
                                        VmManagerIsCrosvm(tmp_config_obj) &&
                                        instance_nums.size() == 1,
                                    calculated_gpu_mode_vec),
            "The set of flags is incompatible with snapshot");

  AndroidEfiLoaderFlag efi_loader =
      AndroidEfiLoaderFlag::FromGlobalGflags(system_image_dir, vm_manager_flag);

  BootloaderFlag bootloader = CF_EXPECT(BootloaderFlag::FromGlobalGflags(
      guest_configs, system_image_dir, vm_manager_flag));

  CF_EXPECT(DiskImageFlagsVectorization(
      tmp_config_obj, fetcher_configs, efi_loader, boot_image, bootloader,
      initramfs_path, kernel_path, super_image, system_image_dir,
      vendor_boot_image));

  return tmp_config_obj;
}

Result<void> SetDefaultFlagsForCrosvm(
    const SystemImageDirFlag& system_image_dir,
    const std::vector<GuestConfig>& guest_configs,
    std::map<std::string, std::string>& name_to_default_value) {
  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());

  std::set<Arch> supported_archs{Arch::X86_64};
  bool default_enable_sandbox =
      supported_archs.find(HostArch()) != supported_archs.end() &&
      EnsureDirectoryExists(kCrosvmVarEmptyDir).ok() &&
      CF_EXPECT(IsDirectoryEmpty(kCrosvmVarEmptyDir)) &&
      !IsRunningInContainer();

  std::string default_enable_sandbox_str = "";
  for (int instance_index = 0; instance_index < instance_nums.size();
       instance_index++) {
    if (instance_index > 0) {
      default_enable_sandbox_str += ",";
    }
    default_enable_sandbox_str += fmt::format("{}", default_enable_sandbox);
  }
  // This is the 1st place to set "enable_sandbox" flag value
  SetCommandLineOptionWithMode("enable_sandbox",
                               default_enable_sandbox_str.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("enable_virtiofs",
                               default_enable_sandbox_str.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  return {};
}

void SetDefaultFlagsForGem5() {
  // TODO: Add support for gem5 gpu models
  SetCommandLineOptionWithMode("gpu_mode",
                               GpuModeString(GpuMode::GuestSwiftshader).c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);

  SetCommandLineOptionWithMode("cpus", "1",
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
}

void SetDefaultFlagsForOpenwrt(Arch target_arch) {
  if (target_arch == Arch::X86_64) {
    SetCommandLineOptionWithMode(
        "ap_kernel_image",
        DefaultHostArtifactsPath("etc/openwrt/images/openwrt_kernel_x86_64")
            .c_str(),
        google::FlagSettingMode::SET_FLAGS_DEFAULT);
    SetCommandLineOptionWithMode(
        "ap_rootfs_image",
        DefaultHostArtifactsPath("etc/openwrt/images/openwrt_rootfs_x86_64")
            .c_str(),
        google::FlagSettingMode::SET_FLAGS_DEFAULT);
  } else if (target_arch == Arch::Arm64) {
    SetCommandLineOptionWithMode(
        "ap_kernel_image",
        DefaultHostArtifactsPath("etc/openwrt/images/openwrt_kernel_aarch64")
            .c_str(),
        google::FlagSettingMode::SET_FLAGS_DEFAULT);
    SetCommandLineOptionWithMode(
        "ap_rootfs_image",
        DefaultHostArtifactsPath("etc/openwrt/images/openwrt_rootfs_aarch64")
            .c_str(),
        google::FlagSettingMode::SET_FLAGS_DEFAULT);
  }
}

Result<void> SetFlagDefaultsForVmm(
    const std::vector<GuestConfig>& guest_configs,
    const SystemImageDirFlag& system_image_dir,
    const VmManagerFlag& vm_manager_flag) {
  // get flag default values and store into map
  auto name_to_default_value = CurrentFlagsToDefaultValue();

  switch (vm_manager_flag.Mode()) {
    case VmmMode::kQemu:
      break;
    case VmmMode::kCrosvm:
      CF_EXPECT(SetDefaultFlagsForCrosvm(system_image_dir, guest_configs,
                                         name_to_default_value));
      break;
    case VmmMode::kGem5:
      CF_EXPECT_EQ(guest_configs[0].target_arch, Arch::Arm64,
                   "Gem5 only supports ARM64");
      SetDefaultFlagsForGem5();
      break;
    case VmmMode::kUnknown:
      return CF_ERR("Unknown VM manager");
  }

  SetDefaultFlagsForOpenwrt(guest_configs[0].target_arch);

  // Set the env variable to empty (in case the caller passed a value for it).
  unsetenv(kCuttlefishConfigEnvVarName);

  return {};
}

Result<Defaults> GetFlagDefaultsFromConfig() {
  if (!FileExists(kDefaultsFilePath)) {
    LOG(INFO) << "SetFlagDefaultsFromConfig: No flag defaults to override.";
    return {};
  }

  return CF_EXPECT(Defaults::FromFile(kDefaultsFilePath));
}

std::string GetConfigFilePath(const CuttlefishConfig& config) {
  return config.AssemblyPath("cuttlefish_config.json");
}

} // namespace cuttlefish
