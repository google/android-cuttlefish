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
#include "host/commands/assemble_cvd/flags.h"

#include <android-base/logging.h>
#include <android-base/parsebool.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>
#include <json/json.h>
#include <json/writer.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>

#include <fruit/fruit.h>
#include <google/protobuf/text_format.h>

#include "launch_cvd.pb.h"

#include "common/libs/utils/base64.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/network.h"
#include "flags.h"
#include "flags_defaults.h"
#include "host/commands/assemble_cvd/alloc.h"
#include "host/commands/assemble_cvd/boot_config.h"
#include "host/commands/assemble_cvd/boot_image_utils.h"
#include "host/commands/assemble_cvd/disk_flags.h"
#include "host/commands/assemble_cvd/display_flags.h"
#include "host/libs/config/config_flag.h"
#include "host/libs/config/esp.h"
#include "host/libs/config/host_tools_version.h"
#include "host/libs/config/instance_nums.h"
#include "host/libs/graphics_detector/graphics_detector.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/gem5_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"
#include "host/libs/vm_manager/vm_manager.h"

using cuttlefish::DefaultHostArtifactsPath;
using cuttlefish::HostBinaryPath;
using cuttlefish::StringFromEnv;
using cuttlefish::vm_manager::CrosvmManager;
using google::FlagSettingMode::SET_FLAGS_DEFAULT;
using google::FlagSettingMode::SET_FLAGS_VALUE;

#define DEFINE_vec DEFINE_string
#define DEFINE_proto DEFINE_string
#define GET_FLAG_STR_VALUE(name) GetFlagStrValueForInstances(FLAGS_ ##name, instances_size, #name, name_to_default_value)
#define GET_FLAG_INT_VALUE(name) GetFlagIntValueForInstances(FLAGS_ ##name, instances_size, #name, name_to_default_value)
#define GET_FLAG_BOOL_VALUE(name) GetFlagBoolValueForInstances(FLAGS_ ##name, instances_size, #name, name_to_default_value)

DEFINE_proto(displays_textproto, CF_DEFAULTS_DISPLAYS_TEXTPROTO,
              "Text Proto input for multi-vd multi-displays");
DEFINE_proto(displays_binproto, CF_DEFAULTS_DISPLAYS_TEXTPROTO,
              "Binary Proto input for multi-vd multi-displays");

DEFINE_vec(cpus, std::to_string(CF_DEFAULTS_CPUS),
              "Virtual CPU count.");
DEFINE_vec(data_policy, CF_DEFAULTS_DATA_POLICY,
              "How to handle userdata partition."
              " Either 'use_existing', 'create_if_missing', 'resize_up_to', or "
              "'always_create'.");
DEFINE_vec(blank_data_image_mb,
              std::to_string(CF_DEFAULTS_BLANK_DATA_IMAGE_MB),
             "The size of the blank data image to generate, MB.");
DEFINE_vec(gdb_port, std::to_string(CF_DEFAULTS_GDB_PORT),
             "Port number to spawn kernel gdb on e.g. -gdb_port=1234. The"
             "kernel must have been built with CONFIG_RANDOMIZE_BASE "
             "disabled.");

// TODO(b/192495477): combine these into a single repeatable '--display' flag
// when assemble_cvd switches to using the new flag parsing library.
DEFINE_string(display0, CF_DEFAULTS_DISPLAY0, cuttlefish::kDisplayHelp);
DEFINE_string(display1, CF_DEFAULTS_DISPLAY1, cuttlefish::kDisplayHelp);
DEFINE_string(display2, CF_DEFAULTS_DISPLAY2, cuttlefish::kDisplayHelp);
DEFINE_string(display3, CF_DEFAULTS_DISPLAY3, cuttlefish::kDisplayHelp);

// TODO(b/171305898): mark these as deprecated after multi-display is fully
// enabled.
DEFINE_string(x_res, "0", "Width of the screen in pixels");
DEFINE_string(y_res, "0", "Height of the screen in pixels");
DEFINE_string(dpi, "0", "Pixels per inch for the screen");
DEFINE_string(refresh_rate_hz, "60", "Screen refresh rate in Hertz");
DEFINE_vec(kernel_path, CF_DEFAULTS_KERNEL_PATH,
              "Path to the kernel. Overrides the one from the boot image");
DEFINE_vec(initramfs_path, CF_DEFAULTS_INITRAMFS_PATH,
              "Path to the initramfs");
DEFINE_string(extra_kernel_cmdline, CF_DEFAULTS_EXTRA_KERNEL_CMDLINE,
              "Additional flags to put on the kernel command line");
DEFINE_string(extra_bootconfig_args, CF_DEFAULTS_EXTRA_BOOTCONFIG_ARGS,
              "Space-separated list of extra bootconfig args. "
              "Note: overwriting an existing bootconfig argument "
              "requires ':=' instead of '='.");
DEFINE_vec(guest_enforce_security,
              cuttlefish::BoolToString(CF_DEFAULTS_GUEST_ENFORCE_SECURITY),
            "Whether to run in enforcing mode (non permissive).");
DEFINE_vec(memory_mb, std::to_string(CF_DEFAULTS_MEMORY_MB),
             "Total amount of memory available for guest, MB.");
DEFINE_vec(serial_number, CF_DEFAULTS_SERIAL_NUMBER,
              "Serial number to use for the device");
DEFINE_vec(use_random_serial, cuttlefish::BoolToString(CF_DEFAULTS_USE_RANDOM_SERIAL),
            "Whether to use random serial for the device.");
DEFINE_vec(vm_manager, CF_DEFAULTS_VM_MANAGER,
              "What virtual machine manager to use, one of {qemu_cli, crosvm}");
DEFINE_vec(gpu_mode, CF_DEFAULTS_GPU_MODE,
              "What gpu configuration to use, one of {auto, drm_virgl, "
              "gfxstream, guest_swiftshader}");
DEFINE_vec(hwcomposer, CF_DEFAULTS_HWCOMPOSER,
              "What hardware composer to use, one of {auto, drm, ranchu} ");
DEFINE_vec(gpu_capture_binary, CF_DEFAULTS_GPU_CAPTURE_BINARY,
              "Path to the GPU capture binary to use when capturing GPU traces"
              "(ngfx, renderdoc, etc)");
DEFINE_vec(enable_gpu_udmabuf, cuttlefish::BoolToString(CF_DEFAULTS_ENABLE_GPU_UDMABUF),
            "Use the udmabuf driver for zero-copy virtio-gpu");
DEFINE_vec(enable_gpu_angle,
           cuttlefish::BoolToString(CF_DEFAULTS_ENABLE_GPU_ANGLE),
           "Use ANGLE to provide GLES implementation (always true for"
           " guest_swiftshader");

DEFINE_vec(use_allocd, CF_DEFAULTS_USE_ALLOCD?"true":"false",
            "Acquire static resources from the resource allocator daemon.");
DEFINE_vec(
    enable_minimal_mode, CF_DEFAULTS_ENABLE_MINIMAL_MODE ? "true" : "false",
    "Only enable the minimum features to boot a cuttlefish device and "
    "support minimal UI interactions.\nNote: Currently only supports "
    "handheld/phone targets");
DEFINE_vec(
    pause_in_bootloader, CF_DEFAULTS_PAUSE_IN_BOOTLOADER?"true":"false",
    "Stop the bootflow in u-boot. You can continue the boot by connecting "
    "to the device console and typing in \"boot\".");
DEFINE_bool(enable_host_bluetooth, CF_DEFAULTS_ENABLE_HOST_BLUETOOTH,
            "Enable the root-canal which is Bluetooth emulator in the host.");
DEFINE_int32(
    rootcanal_instance_num, CF_DEFAULTS_ENABLE_ROOTCANAL_INSTANCE_NUM,
    "If it is greater than 0, use an existing rootcanal instance which is "
    "launched from cuttlefish instance "
    "with rootcanal_instance_num. Else, launch a new rootcanal instance");
DEFINE_string(rootcanal_args, CF_DEFAULTS_ROOTCANAL_ARGS,
              "Space-separated list of rootcanal args. ");
DEFINE_bool(netsim, CF_DEFAULTS_NETSIM,
            "[Experimental] Connect all radios to netsim.");

DEFINE_bool(netsim_bt, CF_DEFAULTS_NETSIM_BT,
            "[Experimental] Connect Bluetooth radio to netsim.");

DEFINE_string(bluetooth_controller_properties_file,
              CF_DEFAULTS_BLUETOOTH_CONTROLLER_PROPERTIES_FILE,
              "The configuartion file path for root-canal which is a Bluetooth "
              "emulator.");
DEFINE_string(
    bluetooth_default_commands_file,
    CF_DEFAULTS_BLUETOOTH_DEFAULT_COMMANDS_FILE,
    "The default commands which root-canal executes when it launches.");

/**
 * crosvm sandbox feature requires /var/empty and seccomp directory
 *
 * Also see SetDefaultFlagsForCrosvm()
 */
DEFINE_vec(
    enable_sandbox, cuttlefish::BoolToString(CF_DEFAULTS_ENABLE_SANDBOX),
    "Enable crosvm sandbox assuming /var/empty and seccomp directories exist. "
    "--noenable-sandbox will disable crosvm sandbox. "
    "When no option is given, sandbox is disabled if Cuttlefish is running "
    "inside a container, or if GPU is enabled (b/152323505), "
    "or if the empty /var/empty directory either does not exist and "
    "cannot be created. Otherwise, sandbox is enabled on the supported "
    "architecture when no option is given.");

DEFINE_string(
    seccomp_policy_dir, CF_DEFAULTS_SECCOMP_POLICY_DIR,
    "With sandbox'ed crosvm, overrieds the security comp policy directory");

DEFINE_vec(start_webrtc, cuttlefish::BoolToString(CF_DEFAULTS_START_WEBRTC),
            "Whether to start the webrtc process.");

DEFINE_vec(webrtc_assets_dir, CF_DEFAULTS_WEBRTC_ASSETS_DIR,
              "[Experimental] Path to WebRTC webpage assets.");

DEFINE_string(webrtc_certs_dir, CF_DEFAULTS_WEBRTC_CERTS_DIR,
              "[Experimental] Path to WebRTC certificates directory.");

static constexpr auto HOST_OPERATOR_SOCKET_PATH = "/run/cuttlefish/operator";

DEFINE_bool(
    // The actual default for this flag is set with SetCommandLineOption() in
    // GetGuestConfigsAndSetDefaults() at the end of this file.
    start_webrtc_sig_server, CF_DEFAULTS_START_WEBRTC_SIG_SERVER,
    "Whether to start the webrtc signaling server. This option only applies to "
    "the first instance, if multiple instances are launched they'll share the "
    "same signaling server, which is owned by the first one.");

DEFINE_string(webrtc_sig_server_addr, CF_DEFAULTS_WEBRTC_SIG_SERVER_ADDR,
              "The address of the webrtc signaling server.");

DEFINE_int32(
    webrtc_sig_server_port, CF_DEFAULTS_WEBRTC_SIG_SERVER_PORT,
    "The port of the signaling server if started outside of this launch. If "
    "-start_webrtc_sig_server is given it will choose 8443+instance_num1-1 and "
    "this parameter is ignored.");

// TODO (jemoreira): We need a much bigger range to reliably support several
// simultaneous connections.
DEFINE_vec(tcp_port_range, CF_DEFAULTS_TCP_PORT_RANGE,
              "The minimum and maximum TCP port numbers to allocate for ICE "
              "candidates as 'min:max'. To use any port just specify '0:0'");

DEFINE_vec(udp_port_range, CF_DEFAULTS_UDP_PORT_RANGE,
              "The minimum and maximum UDP port numbers to allocate for ICE "
              "candidates as 'min:max'. To use any port just specify '0:0'");

DEFINE_string(webrtc_sig_server_path, CF_DEFAULTS_WEBRTC_SIG_SERVER_PATH,
              "The path section of the URL where the device should be "
              "registered with the signaling server.");

DEFINE_bool(
    webrtc_sig_server_secure, CF_DEFAULTS_WEBRTC_SIG_SERVER_SECURE,
    "Whether the WebRTC signaling server uses secure protocols (WSS vs WS).");

DEFINE_bool(verify_sig_server_certificate,
            CF_DEFAULTS_VERIFY_SIG_SERVER_CERTIFICATE,
            "Whether to verify the signaling server's certificate with a "
            "trusted signing authority (Disallow self signed certificates). "
            "This is ignored if an insecure server is configured.");

DEFINE_vec(
    webrtc_device_id, CF_DEFAULTS_WEBRTC_DEVICE_ID,
    "The for the device to register with the signaling server. Every "
    "appearance of the substring '{num}' in the device id will be substituted "
    "with the instance number to support multiple instances");

DEFINE_vec(uuid, CF_DEFAULTS_UUID,
              "UUID to use for the device. Random if not specified");
DEFINE_vec(daemon, CF_DEFAULTS_DAEMON?"true":"false",
            "Run cuttlefish in background, the launcher exits on boot "
            "completed/failed");

DEFINE_vec(setupwizard_mode, CF_DEFAULTS_SETUPWIZARD_MODE,
              "One of DISABLED,OPTIONAL,REQUIRED");
DEFINE_vec(enable_bootanimation,
           cuttlefish::BoolToString(CF_DEFAULTS_ENABLE_BOOTANIMATION),
           "Whether to enable the boot animation.");

DEFINE_string(qemu_binary_dir, CF_DEFAULTS_QEMU_BINARY_DIR,
              "Path to the directory containing the qemu binary to use");
DEFINE_string(crosvm_binary, CF_DEFAULTS_CROSVM_BINARY,
              "The Crosvm binary to use");
DEFINE_vec(gem5_binary_dir, CF_DEFAULTS_GEM5_BINARY_DIR,
              "Path to the gem5 build tree root");
DEFINE_vec(gem5_checkpoint_dir, CF_DEFAULTS_GEM5_CHECKPOINT_DIR,
              "Path to the gem5 restore checkpoint directory");
DEFINE_vec(gem5_debug_file, CF_DEFAULTS_GEM5_DEBUG_FILE,
              "The file name where gem5 saves debug prints and logs");
DEFINE_string(gem5_debug_flags, CF_DEFAULTS_GEM5_DEBUG_FLAGS,
              "The debug flags gem5 uses to print debugs to file");

DEFINE_vec(restart_subprocesses,
              cuttlefish::BoolToString(CF_DEFAULTS_RESTART_SUBPROCESSES),
              "Restart any crashed host process");
DEFINE_vec(enable_vehicle_hal_grpc_server,
            cuttlefish::BoolToString(CF_DEFAULTS_ENABLE_VEHICLE_HAL_GRPC_SERVER),
            "Enables the vehicle HAL "
            "emulation gRPC server on the host");
DEFINE_vec(bootloader, CF_DEFAULTS_BOOTLOADER, "Bootloader binary path");
DEFINE_vec(boot_slot, CF_DEFAULTS_BOOT_SLOT,
              "Force booting into the given slot. If empty, "
              "the slot will be chosen based on the misc partition if using a "
              "bootloader. It will default to 'a' if empty and not using a "
              "bootloader.");
DEFINE_int32(num_instances, CF_DEFAULTS_NUM_INSTANCES,
             "Number of Android guests to launch");
DEFINE_string(instance_nums, CF_DEFAULTS_INSTANCE_NUMS,
              "A comma-separated list of instance numbers "
              "to use. Mutually exclusive with base_instance_num.");
DEFINE_string(report_anonymous_usage_stats,
              CF_DEFAULTS_REPORT_ANONYMOUS_USAGE_STATS,
              "Report anonymous usage "
              "statistics for metrics collection and analysis.");
DEFINE_vec(ril_dns, CF_DEFAULTS_RIL_DNS,
              "DNS address of mobile network (RIL)");
DEFINE_vec(kgdb, cuttlefish::BoolToString(CF_DEFAULTS_KGDB),
            "Configure the virtual device for debugging the kernel "
            "with kgdb/kdb. The kernel must have been built with "
            "kgdb support, and serial console must be enabled.");

DEFINE_vec(start_gnss_proxy, cuttlefish::BoolToString(CF_DEFAULTS_START_GNSS_PROXY),
            "Whether to start the gnss proxy.");

DEFINE_vec(gnss_file_path, CF_DEFAULTS_GNSS_FILE_PATH,
              "Local gnss raw measurement file path for the gnss proxy");

DEFINE_vec(fixed_location_file_path, CF_DEFAULTS_FIXED_LOCATION_FILE_PATH,
              "Local fixed location file path for the gnss proxy");

// by default, this modem-simulator is disabled
DEFINE_vec(enable_modem_simulator,
              CF_DEFAULTS_ENABLE_MODEM_SIMULATOR ? "true" : "false",
              "Enable the modem simulator to process RILD AT commands");
// modem_simulator_sim_type=2 for test CtsCarrierApiTestCases
DEFINE_vec(modem_simulator_sim_type,
              std::to_string(CF_DEFAULTS_MODEM_SIMULATOR_SIM_TYPE),
              "Sim type: 1 for normal, 2 for CtsCarrierApiTestCases");

DEFINE_vec(console, cuttlefish::BoolToString(CF_DEFAULTS_CONSOLE),
              "Enable the serial console");

DEFINE_vec(enable_kernel_log,
           cuttlefish::BoolToString(CF_DEFAULTS_ENABLE_KERNEL_LOG),
            "Enable kernel console/dmesg logging");

DEFINE_vec(vhost_net, cuttlefish::BoolToString(CF_DEFAULTS_VHOST_NET),
            "Enable vhost acceleration of networking");

DEFINE_string(
    vhost_user_mac80211_hwsim, CF_DEFAULTS_VHOST_USER_MAC80211_HWSIM,
    "Unix socket path for vhost-user of mac80211_hwsim, typically served by "
    "wmediumd. You can set this when using an external wmediumd instance.");
DEFINE_string(wmediumd_config, CF_DEFAULTS_WMEDIUMD_CONFIG,
              "Path to the wmediumd config file. When missing, the default "
              "configuration is used which adds MAC addresses for up to 16 "
              "cuttlefish instances including AP.");

DEFINE_string(ap_rootfs_image, CF_DEFAULTS_AP_ROOTFS_IMAGE,
              "rootfs image for AP instance");
DEFINE_string(ap_kernel_image, CF_DEFAULTS_AP_KERNEL_IMAGE,
              "kernel image for AP instance");

DEFINE_vec(record_screen, cuttlefish::BoolToString(CF_DEFAULTS_RECORD_SCREEN),
           "Enable screen recording. "
           "Requires --start_webrtc");

DEFINE_vec(smt, cuttlefish::BoolToString(CF_DEFAULTS_SMT),
           "Enable simultaneous multithreading (SMT/HT)");

DEFINE_vec(
    vsock_guest_cid, std::to_string(CF_DEFAULTS_VSOCK_GUEST_CID),
    "vsock_guest_cid is used to determine the guest vsock cid as well as all "
    "the ports"
    "of all vsock servers such as tombstone or modem simulator(s)."
    "The vsock ports and guest vsock cid are a function of vsock_guest_cid and "
    "instance number."
    "An instance number of i th instance is determined by --num_instances=N "
    "and --base_instance_num=B"
    "The instance number of i th instance is B + i where i in [0, N-1] and B "
    ">= 1."
    "See --num_instances, and --base_instance_num for more information"
    "If --vsock_guest_cid=C is given and C >= 3, the guest vsock cid is C + i. "
    "Otherwise,"
    "the guest vsock cid is 2 + instance number, which is 2 + (B + i)."
    "If --vsock_guest_cid is not given, each vsock server port number for i th "
    "instance is"
    "base + instance number - 1. vsock_guest_cid is by default B + i + 2."
    "Thus, by default, each port is base + vsock_guest_cid - 3."
    "The same formula holds when --vsock_guest_cid=C is given, for algorithm's "
    "sake."
    "Each vsock server port number is base + C - 3.");

DEFINE_string(secure_hals, CF_DEFAULTS_SECURE_HALS,
              "Which HALs to use enable host security features for. Supports "
              "keymint and gatekeeper at the moment.");

DEFINE_vec(use_sdcard, CF_DEFAULTS_USE_SDCARD?"true":"false",
            "Create blank SD-Card image and expose to guest");

DEFINE_vec(protected_vm, cuttlefish::BoolToString(CF_DEFAULTS_PROTECTED_VM),
            "Boot in Protected VM mode");

DEFINE_vec(enable_audio, cuttlefish::BoolToString(CF_DEFAULTS_ENABLE_AUDIO),
            "Whether to play or capture audio");

DEFINE_vec(camera_server_port, std::to_string(CF_DEFAULTS_CAMERA_SERVER_PORT),
              "camera vsock port");

DEFINE_vec(userdata_format, CF_DEFAULTS_USERDATA_FORMAT,
              "The userdata filesystem format");

DEFINE_bool(use_overlay, CF_DEFAULTS_USE_OVERLAY,
            "Capture disk writes an overlay. This is a "
            "prerequisite for powerwash_cvd or multiple instances.");

DEFINE_vec(modem_simulator_count,
              std::to_string(CF_DEFAULTS_MODEM_SIMULATOR_COUNT),
              "Modem simulator count corresponding to maximum sim number");

DECLARE_string(assembly_dir);
DECLARE_string(boot_image);
DECLARE_string(system_image_dir);

namespace cuttlefish {
using vm_manager::QemuManager;
using vm_manager::Gem5Manager;
using vm_manager::GetVmManager;

namespace {

std::pair<uint16_t, uint16_t> ParsePortRange(const std::string& flag) {
  static const std::regex rgx("[0-9]+:[0-9]+");
  CHECK(std::regex_match(flag, rgx))
      << "Port range flag has invalid value: " << flag;
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

#ifdef __ANDROID__
Result<std::vector<GuestConfig>> ReadGuestConfig() {
  std::vector<GuestConfig> rets;
  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());
  for (int instance_index = 0; instance_index < instance_nums.size(); instance_index++) {
    // QEMU isn't on Android, so always follow host arch
    GuestConfig ret{};
    ret.target_arch = HostArch();
    ret.bootconfig_supported = true;
    ret.android_version_number = "0.0.0";
    rets.push_back(ret);
  }
  return rets;
}
#else
Result<std::vector<GuestConfig>> ReadGuestConfig() {
  std::vector<GuestConfig> guest_configs;
  std::vector<std::string> boot_image =
      android::base::Split(FLAGS_boot_image, ",");
  std::vector<std::string> kernel_path =
      android::base::Split(FLAGS_kernel_path, ",");
  std::string kernel_image_path = "";
  std::string cur_boot_image;
  std::string cur_kernel_path;

  std::string current_path = StringFromEnv("PATH", "");
  std::string bin_folder = DefaultHostArtifactsPath("bin");
  std::string new_path = "PATH=";
  new_path += current_path;
  new_path += ":";
  new_path += bin_folder;
  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());
  for (int instance_index = 0; instance_index < instance_nums.size(); instance_index++) {
    // extract-ikconfig can be called directly on the boot image since it looks
    // for the ikconfig header in the image before extracting the config list.
    // This code is liable to break if the boot image ever includes the
    // ikconfig header outside the kernel.
    cur_kernel_path = "";
    if (instance_index < kernel_path.size()) {
      cur_kernel_path = kernel_path[instance_index];
    }

    cur_boot_image = "";
    if (instance_index < boot_image.size()) {
      cur_boot_image = boot_image[instance_index];
    }

    if (cur_kernel_path.size() > 0) {
      kernel_image_path = cur_kernel_path;
    } else if (cur_boot_image.size() > 0) {
      kernel_image_path = cur_boot_image;
    }

    Command ikconfig_cmd(HostBinaryPath("extract-ikconfig"));
    ikconfig_cmd.AddParameter(kernel_image_path);
    ikconfig_cmd.SetEnvironment({new_path});

    std::string ikconfig_path =
        StringFromEnv("TEMP", "/tmp") + "/ikconfig.XXXXXX";
    auto ikconfig_fd = SharedFD::Mkstemp(&ikconfig_path);
    CF_EXPECT(ikconfig_fd->IsOpen(),
              "Unable to create ikconfig file: " << ikconfig_fd->StrError());
    ikconfig_cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, ikconfig_fd);

    auto ikconfig_proc = ikconfig_cmd.Start();
    CF_EXPECT(ikconfig_proc.Started() && ikconfig_proc.Wait() == 0,
              "Failed to extract ikconfig from " << kernel_image_path);

    std::string config = ReadFile(ikconfig_path);

    GuestConfig guest_config;
    if (config.find("\nCONFIG_ARM=y") != std::string::npos) {
      guest_config.target_arch = Arch::Arm;
    } else if (config.find("\nCONFIG_ARM64=y") != std::string::npos) {
      guest_config.target_arch = Arch::Arm64;
    } else if (config.find("\nCONFIG_ARCH_RV64I=y") != std::string::npos) {
      guest_config.target_arch = Arch::RiscV64;
    } else if (config.find("\nCONFIG_X86_64=y") != std::string::npos) {
      guest_config.target_arch = Arch::X86_64;
    } else if (config.find("\nCONFIG_X86=y") != std::string::npos) {
      guest_config.target_arch = Arch::X86;
    } else {
      return CF_ERR("Unknown target architecture");
    }
    guest_config.bootconfig_supported =
        config.find("\nCONFIG_BOOT_CONFIG=y") != std::string::npos;
    // Once all Cuttlefish kernel versions are at least 5.15, this code can be
    // removed. CONFIG_CRYPTO_HCTR2=y will always be set.
    guest_config.hctr2_supported =
        config.find("\nCONFIG_CRYPTO_HCTR2=y") != std::string::npos;

    unlink(ikconfig_path.c_str());
    guest_config.android_version_number =
        CF_EXPECT(ReadAndroidVersionFromBootImage(cur_boot_image),
                  "Failed to read guest's android version");
    ;
    guest_configs.push_back(guest_config);
  }
  return guest_configs;
}

#endif  // #ifdef __ANDROID__

template <typename ProtoType>
Result<ProtoType> ParseTextProtoFlagHelper(const std::string& flag_value,
                                       const std::string& flag_name) {
  ProtoType proto_result;
  google::protobuf::TextFormat::Parser p;
  CF_EXPECT(p.ParseFromString(flag_value, &proto_result),
            "Failed to parse: " << flag_name << ", value: " << flag_value);
  return proto_result;
}

template <typename ProtoType>
Result<ProtoType> ParseBinProtoFlagHelper(const std::string& flag_value,
                                       const std::string& flag_name) {
  ProtoType proto_result;
  std::vector<uint8_t> output;
  CF_EXPECT(DecodeBase64(flag_value, &output));
  std::string serialized = std::string(output.begin(), output.end());

  CF_EXPECT(proto_result.ParseFromString(serialized),
            "Failed to parse binary proto, flag: "<< flag_name << ", value: " << flag_value);
  return proto_result;
}

Result<std::vector<std::vector<CuttlefishConfig::DisplayConfig>>>
    ParseDisplaysProto() {
  auto proto_result = FLAGS_displays_textproto.empty() ? \
  ParseBinProtoFlagHelper<InstancesDisplays>(FLAGS_displays_binproto, "displays_binproto") : \
  ParseTextProtoFlagHelper<InstancesDisplays>(FLAGS_displays_textproto, "displays_textproto");

  std::vector<std::vector<CuttlefishConfig::DisplayConfig>> result;
  for (int i=0; i<proto_result->instances_size(); i++) {
    std::vector<CuttlefishConfig::DisplayConfig> display_configs;
    const InstanceDisplays& launch_cvd_instance = proto_result->instances(i);
    for (int display_num=0; display_num<launch_cvd_instance.displays_size(); display_num++) {
      const InstanceDisplay& display = launch_cvd_instance.displays(display_num);

      // use same code logic from ParseDisplayConfig
      int display_dpi = CF_DEFAULTS_DISPLAY_DPI;
      if (display.dpi() != 0) {
        display_dpi = display.dpi();
      }

      int display_refresh_rate_hz = CF_DEFAULTS_DISPLAY_REFRESH_RATE;
      if (display.refresh_rate_hertz() != 0) {
        display_refresh_rate_hz = display.refresh_rate_hertz();
      }

      display_configs.push_back(CuttlefishConfig::DisplayConfig{
        .width = display.width(),
        .height = display.height(),
        .dpi = display_dpi,
        .refresh_rate_hz = display_refresh_rate_hz,
        });
    }
    result.push_back(display_configs);
  }
  return result;
}

Result<bool> ParseBool(const std::string& flag_str,
                        const std::string& flag_name) {
  auto result = android::base::ParseBool(flag_str);
  CF_EXPECT(result != android::base::ParseBoolResult::kError,
            "Failed to parse value \"" << flag_str
            << "\" for " << flag_name);
  if (result == android::base::ParseBoolResult::kTrue) {
    return true;
  }
  return false;
}

Result<std::unordered_map<int, std::string>> CreateNumToWebrtcDeviceIdMap(
    const CuttlefishConfig& tmp_config_obj,
    const std::set<std::int32_t>& instance_nums,
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
    device_ids = std::move(
        std::vector<std::string>(instance_nums.size(), tokens.front()));
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
    const std::string& flag_values, int32_t instances_size, const std::string& flag_name,
    std::map<std::string, std::string>& name_to_default_value) {
  std::vector<std::string> flag_vec = android::base::Split(flag_values, ",");
  std::vector<bool> value_vec(instances_size);

  CF_EXPECT(name_to_default_value.find(flag_name) != name_to_default_value.end());
  std::vector<std::string> default_value_vec =  android::base::Split(name_to_default_value[flag_name], ",");

  for (int instance_index=0; instance_index<instances_size; instance_index++) {
    if (instance_index >= flag_vec.size()) {
      value_vec[instance_index] = CF_EXPECT(ParseBool(flag_vec[0], flag_name));
    } else {
      if (flag_vec[instance_index] == "unset" || flag_vec[instance_index] == "\"unset\"") {
        std::string default_value = default_value_vec[0];
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
    const std::string& flag_values, int32_t instances_size, const std::string& flag_name,
    std::map<std::string, std::string>& name_to_default_value) {
  std::vector<std::string> flag_vec = android::base::Split(flag_values, ",");
  std::vector<int> value_vec(instances_size);

  CF_EXPECT(name_to_default_value.find(flag_name) != name_to_default_value.end());
  std::vector<std::string> default_value_vec =  android::base::Split(name_to_default_value[flag_name], ",");

  for (int instance_index=0; instance_index<instances_size; instance_index++) {
    if (instance_index >= flag_vec.size()) {
      CF_EXPECT(android::base::ParseInt(flag_vec[0].c_str(), &value_vec[instance_index]),
      "Failed to parse value \"" << flag_vec[0] << "\" for " << flag_name);
    } else {
      if (flag_vec[instance_index] == "unset" || flag_vec[instance_index] == "\"unset\"") {
        std::string default_value = default_value_vec[0];
        if (instance_index < default_value_vec.size()) {
          default_value = default_value_vec[instance_index];
        }
        CF_EXPECT(android::base::ParseInt(default_value,
        &value_vec[instance_index]),
        "Failed to parse value \"" << default_value << "\" for " << flag_name);
      } else {
        CF_EXPECT(android::base::ParseInt(flag_vec[instance_index].c_str(),
        &value_vec[instance_index]),
        "Failed to parse value \"" << flag_vec[instance_index] << "\" for " << flag_name);
      }
    }
  }
  return value_vec;
}

Result<std::vector<std::string>> GetFlagStrValueForInstances(
    const std::string& flag_values, int32_t instances_size,
    const std::string& flag_name, std::map<std::string, std::string>& name_to_default_value) {
  std::vector<std::string> flag_vec = android::base::Split(flag_values, ",");
  std::vector<std::string> value_vec(instances_size);

  CF_EXPECT(name_to_default_value.find(flag_name) != name_to_default_value.end());
  std::vector<std::string> default_value_vec =  android::base::Split(name_to_default_value[flag_name], ",");

  for (int instance_index=0; instance_index<instances_size; instance_index++) {
    if (instance_index >= flag_vec.size()) {
      value_vec[instance_index] = flag_vec[0];
    } else {
      if (flag_vec[instance_index] == "unset" || flag_vec[instance_index] == "\"unset\"") {
        std::string default_value = default_value_vec[0];
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

} // namespace

Result<CuttlefishConfig> InitializeCuttlefishConfiguration(
    const std::string& root_dir,
    const std::vector<GuestConfig>& guest_configs,
    fruit::Injector<>& injector, const FetcherConfig& fetcher_config) {
  CuttlefishConfig tmp_config_obj;

  for (const auto& fragment : injector.getMultibindings<ConfigFragment>()) {
    CHECK(tmp_config_obj.SaveFragment(*fragment))
        << "Failed to save fragment " << fragment->Name();
  }

  tmp_config_obj.set_root_dir(root_dir);

  // TODO(weihsu), b/250988697:
  // FLAGS_vm_manager used too early, have to handle this vectorized string early
  // Currently, all instances should use same vmm, added checking here
  std::vector<std::string> vm_manager_vec =
      android::base::Split(FLAGS_vm_manager, ",");
  for (int i=1; i<vm_manager_vec.size(); i++) {
    CF_EXPECT(
        vm_manager_vec[0] == vm_manager_vec[i],
        "All instances should have same vm_manager, " << FLAGS_vm_manager);
  }

  // TODO(weihsu), b/250988697: moved bootconfig_supported and hctr2_supported
  // into each instance, but target_arch is still in todo
  // target_arch should be in instance later
  auto vmm = GetVmManager(vm_manager_vec[0], guest_configs[0].target_arch);
  if (!vmm) {
    LOG(FATAL) << "Invalid vm_manager: " << vm_manager_vec[0];
  }
  tmp_config_obj.set_vm_manager(vm_manager_vec[0]);

  const GraphicsAvailability graphics_availability =
    GetGraphicsAvailabilityWithSubprocessCheck();

  LOG(DEBUG) << graphics_availability;

  auto secure_hals = android::base::Split(FLAGS_secure_hals, ",");
  tmp_config_obj.set_secure_hals(
      std::set<std::string>(secure_hals.begin(), secure_hals.end()));

  tmp_config_obj.set_extra_kernel_cmdline(FLAGS_extra_kernel_cmdline);
  tmp_config_obj.set_extra_bootconfig_args(FLAGS_extra_bootconfig_args);

  tmp_config_obj.set_host_tools_version(HostToolsCrc());

  tmp_config_obj.set_gem5_debug_flags(FLAGS_gem5_debug_flags);

  // streaming, webrtc setup
  tmp_config_obj.set_webrtc_certs_dir(FLAGS_webrtc_certs_dir);
  tmp_config_obj.set_sig_server_secure(FLAGS_webrtc_sig_server_secure);
  // Note: This will be overridden if the sig server is started by us
  tmp_config_obj.set_sig_server_port(FLAGS_webrtc_sig_server_port);
  tmp_config_obj.set_sig_server_address(FLAGS_webrtc_sig_server_addr);
  tmp_config_obj.set_sig_server_path(FLAGS_webrtc_sig_server_path);
  tmp_config_obj.set_sig_server_strict(FLAGS_verify_sig_server_certificate);

  tmp_config_obj.set_enable_metrics(FLAGS_report_anonymous_usage_stats);

  tmp_config_obj.set_vhost_user_mac80211_hwsim(FLAGS_vhost_user_mac80211_hwsim);

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

  tmp_config_obj.set_wmediumd_config(FLAGS_wmediumd_config);

  // netsim flags allow all radios or selecting a specific radio
  tmp_config_obj.set_rootcanal_default_commands_file(
      FLAGS_bluetooth_default_commands_file);
  tmp_config_obj.set_rootcanal_config_file(
      FLAGS_bluetooth_controller_properties_file);

  bool is_any_netsim = FLAGS_netsim || FLAGS_netsim_bt;
  bool is_bt_netsim = FLAGS_netsim || FLAGS_netsim_bt;

  // crosvm should create fifos for Bluetooth
  tmp_config_obj.set_enable_host_bluetooth(FLAGS_enable_host_bluetooth || is_bt_netsim);

  // rootcanal and bt_connector should handle Bluetooth (instead of netsim)
  tmp_config_obj.set_enable_host_bluetooth_connector(FLAGS_enable_host_bluetooth && !is_bt_netsim);

  // These flags inform NetsimServer::ResultSetup which radios it owns.
  if (is_bt_netsim) {
    tmp_config_obj.netsim_radio_enable(CuttlefishConfig::NetsimRadio::Bluetooth);
  }
  // end of vectorize ap_rootfs_image, ap_kernel_image, wmediumd_config

  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());

  // get flag default values and store into map
  auto name_to_default_value = CurrentFlagsToDefaultValue();
  // old flags but vectorized for multi-device instances
  int32_t instances_size = instance_nums.size();
  std::vector<std::string> gnss_file_paths =
      CF_EXPECT(GET_FLAG_STR_VALUE(gnss_file_path));
  std::vector<std::string> fixed_location_file_paths =
      CF_EXPECT(GET_FLAG_STR_VALUE(fixed_location_file_path));
  std::vector<int> x_res_vec = CF_EXPECT(GET_FLAG_INT_VALUE(x_res));
  std::vector<int> y_res_vec = CF_EXPECT(GET_FLAG_INT_VALUE(y_res));
  std::vector<int> dpi_vec = CF_EXPECT(GET_FLAG_INT_VALUE(dpi));
  std::vector<int> refresh_rate_hz_vec = CF_EXPECT(GET_FLAG_INT_VALUE(
      refresh_rate_hz));
  std::vector<int> memory_mb_vec = CF_EXPECT(GET_FLAG_INT_VALUE(memory_mb));
  std::vector<int> camera_server_port_vec = CF_EXPECT(GET_FLAG_INT_VALUE(
      camera_server_port));
  std::vector<int> vsock_guest_cid_vec = CF_EXPECT(GET_FLAG_INT_VALUE(
      vsock_guest_cid));
  std::vector<int> cpus_vec = CF_EXPECT(GET_FLAG_INT_VALUE(cpus));
  std::vector<int> blank_data_image_mb_vec = CF_EXPECT(GET_FLAG_INT_VALUE(
      blank_data_image_mb));
  std::vector<int> gdb_port_vec = CF_EXPECT(GET_FLAG_INT_VALUE(gdb_port));
  std::vector<std::string> setupwizard_mode_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(setupwizard_mode));
  std::vector<std::string> userdata_format_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(userdata_format));
  std::vector<bool> guest_enforce_security_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      guest_enforce_security));
  std::vector<bool> use_random_serial_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      use_random_serial));
  std::vector<bool> use_allocd_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(use_allocd));
  std::vector<bool> use_sdcard_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(use_sdcard));
  std::vector<bool> pause_in_bootloader_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      pause_in_bootloader));
  std::vector<bool> daemon_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(daemon));
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
  std::vector<bool> enable_vehicle_hal_grpc_server_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      enable_vehicle_hal_grpc_server));
  std::vector<bool> start_gnss_proxy_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      start_gnss_proxy));
  std::vector<bool> enable_bootanimation_vec =
      CF_EXPECT(GET_FLAG_BOOL_VALUE(enable_bootanimation));
  std::vector<bool> record_screen_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      record_screen));
  std::vector<std::string> gem5_debug_file_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(gem5_debug_file));
  std::vector<bool> protected_vm_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      protected_vm));
  std::vector<bool> enable_kernel_log_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      enable_kernel_log));
  std::vector<bool> kgdb_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(kgdb));
  std::vector<std::string> boot_slot_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(boot_slot));
  std::vector<bool> start_webrtc_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      start_webrtc));
  std::vector<std::string> webrtc_assets_dir_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(webrtc_assets_dir));
  std::vector<std::string> tcp_port_range_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(tcp_port_range));
  std::vector<std::string> udp_port_range_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(udp_port_range));
  std::vector<bool> vhost_net_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      vhost_net));
  std::vector<std::string> ril_dns_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(ril_dns));

  // At this time, FLAGS_enable_sandbox comes from SetDefaultFlagsForCrosvm
  std::vector<bool> enable_sandbox_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      enable_sandbox));

  std::vector<std::string> gpu_mode_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(gpu_mode));
  std::vector<std::string> gpu_capture_binary_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(gpu_capture_binary));
  std::vector<bool> restart_subprocesses_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      restart_subprocesses));
  std::vector<std::string> hwcomposer_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(hwcomposer));
  std::vector<bool> enable_gpu_udmabuf_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      enable_gpu_udmabuf));
  std::vector<bool> enable_gpu_angle_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      enable_gpu_angle));
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
  std::vector<std::string> data_policy_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(data_policy));

  // multi-dv multi-display proto input
  std::vector<std::vector<CuttlefishConfig::DisplayConfig>> instances_display_configs;
  if (!FLAGS_displays_textproto.empty() || !FLAGS_displays_binproto.empty()) {
    instances_display_configs = CF_EXPECT(ParseDisplaysProto());
  }

  std::string default_enable_sandbox = "";
  std::string comma_str = "";

  CHECK(FLAGS_use_overlay || instance_nums.size() == 1)
      << "`--use_overlay=false` is incompatible with multiple instances";
  CHECK(instance_nums.size() > 0) << "Require at least one instance.";
  auto rootcanal_instance_num = *instance_nums.begin() - 1;
  if (FLAGS_rootcanal_instance_num > 0) {
    rootcanal_instance_num = FLAGS_rootcanal_instance_num - 1;
  }
  tmp_config_obj.set_rootcanal_args(FLAGS_rootcanal_args);
  tmp_config_obj.set_rootcanal_hci_port(7300 + rootcanal_instance_num);
  tmp_config_obj.set_rootcanal_link_port(7400 + rootcanal_instance_num);
  tmp_config_obj.set_rootcanal_test_port(7500 + rootcanal_instance_num);
  tmp_config_obj.set_rootcanal_link_ble_port(7600 + rootcanal_instance_num);
  LOG(DEBUG) << "rootcanal_instance_num: " << rootcanal_instance_num;
  LOG(DEBUG) << "launch rootcanal: " << (FLAGS_rootcanal_instance_num <= 0);
  bool is_first_instance = true;
  int instance_index = 0;
  auto num_to_webrtc_device_id_flag_map =
      CF_EXPECT(CreateNumToWebrtcDeviceIdMap(tmp_config_obj, instance_nums,
                                             FLAGS_webrtc_device_id));
  for (const auto& num : instance_nums) {
    IfaceConfig iface_config;
    if (use_allocd_vec[instance_index]) {
      auto iface_opt = AllocateNetworkInterfaces();
      if (!iface_opt.has_value()) {
        LOG(FATAL) << "Failed to acquire network interfaces";
      }
      iface_config = iface_opt.value();
    } else {
      iface_config = DefaultNetworkInterfaces(num);
    }


    auto instance = tmp_config_obj.ForInstance(num);
    auto const_instance =
        const_cast<const CuttlefishConfig&>(tmp_config_obj).ForInstance(num);

    instance.set_bootconfig_supported(guest_configs[instance_index].bootconfig_supported);
    instance.set_filename_encryption_mode(
      guest_configs[instance_index].hctr2_supported ? "hctr2" : "cts");
    instance.set_use_allocd(use_allocd_vec[instance_index]);
    instance.set_enable_audio(enable_audio_vec[instance_index]);
    instance.set_enable_vehicle_hal_grpc_server(
      enable_vehicle_hal_grpc_server_vec[instance_index]);
    instance.set_enable_gnss_grpc_proxy(start_gnss_proxy_vec[instance_index]);
    instance.set_enable_bootanimation(enable_bootanimation_vec[instance_index]);
    instance.set_record_screen(record_screen_vec[instance_index]);
    instance.set_gem5_debug_file(gem5_debug_file_vec[instance_index]);
    instance.set_protected_vm(protected_vm_vec[instance_index]);
    instance.set_enable_kernel_log(enable_kernel_log_vec[instance_index]);
    if (!boot_slot_vec[instance_index].empty()) {
      instance.set_boot_slot(boot_slot_vec[instance_index]);
    }

    instance.set_crosvm_binary(crosvm_binary_vec[instance_index]);
    instance.set_seccomp_policy_dir(seccomp_policy_dir_vec[instance_index]);
    instance.set_qemu_binary_dir(qemu_binary_dir_vec[instance_index]);

    // wifi, bluetooth, connectivity setup
    instance.set_ril_dns(ril_dns_vec[instance_index]);

    instance.set_vhost_net(vhost_net_vec[instance_index]);
    // end of wifi, bluetooth, connectivity setup

    if (use_random_serial_vec[instance_index]) {
      instance.set_serial_number(
          RandomSerialNumber("CFCVD" + std::to_string(num)));
    } else {
      instance.set_serial_number(FLAGS_serial_number + std::to_string(num));
    }

    instance.set_grpc_socket_path(const_instance.PerInstanceGrpcSocketPath(""));

    // call this before all stuff that has vsock server: e.g. touchpad, keyboard, etc
    const auto vsock_guest_cid = vsock_guest_cid_vec[instance_index] + num - GetInstance();
    instance.set_vsock_guest_cid(vsock_guest_cid);
    auto calc_vsock_port = [vsock_guest_cid](const int base_port) {
      // a base (vsock) port is like 9600 for modem_simulator, etc
      return cuttlefish::GetVsockServerPort(base_port, vsock_guest_cid);
    };
    instance.set_session_id(iface_config.mobile_tap.session_id);

    instance.set_cpus(cpus_vec[instance_index]);
    // make sure all instances have multiple of 2 then SMT mode
    // if any of instance doesn't have multiple of 2 then NOT SMT
    CF_EXPECT(!smt_vec[instance_index] || cpus_vec[instance_index] % 2 == 0,
              "CPUs must be a multiple of 2 in SMT mode");
    instance.set_smt(smt_vec[instance_index]);

    // new instance specific flags (moved from common flags)
    CF_EXPECT(instance_index < guest_configs.size(),
              "instance_index " << instance_index << " out of boundary "
                                << guest_configs.size());
    instance.set_target_arch(guest_configs[instance_index].target_arch);
    instance.set_guest_android_version(
        guest_configs[instance_index].android_version_number);
    instance.set_console(console_vec[instance_index]);
    instance.set_kgdb(console_vec[instance_index] && kgdb_vec[instance_index]);
    instance.set_blank_data_image_mb(blank_data_image_mb_vec[instance_index]);
    instance.set_gdb_port(gdb_port_vec[instance_index]);

    std::vector<CuttlefishConfig::DisplayConfig> display_configs;
    // assume displays proto input has higher priority than original display inputs
    if (!FLAGS_displays_textproto.empty() || !FLAGS_displays_binproto.empty()) {
      if (instance_index < instances_display_configs.size()) {
        display_configs = instances_display_configs[instance_index];
      } // else display_configs is an empty vector
    } else {
      auto display0 = CF_EXPECT(ParseDisplayConfig(FLAGS_display0));
      if (display0) {
        display_configs.push_back(*display0);
      }
      auto display1 = CF_EXPECT(ParseDisplayConfig(FLAGS_display1));
      if (display1) {
        display_configs.push_back(*display1);
      }
      auto display2 = CF_EXPECT(ParseDisplayConfig(FLAGS_display2));
      if (display2) {
        display_configs.push_back(*display2);
      }
      auto display3 = CF_EXPECT(ParseDisplayConfig(FLAGS_display3));
      if (display3) {
        display_configs.push_back(*display3);
      }
    }

    if (x_res_vec[instance_index] > 0 && y_res_vec[instance_index] > 0) {
      if (display_configs.empty()) {
        display_configs.push_back({
            .width = x_res_vec[instance_index],
            .height = y_res_vec[instance_index],
            .dpi = dpi_vec[instance_index],
            .refresh_rate_hz = refresh_rate_hz_vec[instance_index],
          });
      } else {
        LOG(WARNING) << "Ignoring --x_res and --y_res when --displayN specified.";
      }
    }
    instance.set_display_configs(display_configs);

    instance.set_memory_mb(memory_mb_vec[instance_index]);
    instance.set_ddr_mem_mb(memory_mb_vec[instance_index] * 2);
    instance.set_setupwizard_mode(setupwizard_mode_vec[instance_index]);
    instance.set_userdata_format(userdata_format_vec[instance_index]);
    instance.set_guest_enforce_security(guest_enforce_security_vec[instance_index]);
    instance.set_pause_in_bootloader(pause_in_bootloader_vec[instance_index]);
    instance.set_run_as_daemon(daemon_vec[instance_index]);
    instance.set_enable_modem_simulator(enable_modem_simulator_vec[instance_index] &&
                                        !enable_minimal_mode_vec[instance_index]);
    instance.set_modem_simulator_instance_number(modem_simulator_count_vec[instance_index]);
    instance.set_modem_simulator_sim_type(modem_simulator_sim_type_vec[instance_index]);

    instance.set_enable_minimal_mode(enable_minimal_mode_vec[instance_index]);
    instance.set_camera_server_port(camera_server_port_vec[instance_index]);
    instance.set_gem5_binary_dir(gem5_binary_dir_vec[instance_index]);
    instance.set_gem5_checkpoint_dir(gem5_checkpoint_dir_vec[instance_index]);
    instance.set_data_policy(data_policy_vec[instance_index]);

    instance.set_mobile_bridge_name(StrForInstance("cvd-mbr-", num));
    instance.set_wifi_bridge_name("cvd-wbr");
    instance.set_ethernet_bridge_name("cvd-ebr");
    instance.set_mobile_tap_name(iface_config.mobile_tap.name);

#ifdef ENFORCE_MAC80211_HWSIM
    const bool enforce_mac80211_hwsim = true;
#else
    const bool enforce_mac80211_hwsim = false;
#endif
    if (NetworkInterfaceExists(iface_config.non_bridged_wireless_tap.name) &&
        enforce_mac80211_hwsim) {
      instance.set_use_bridged_wifi_tap(false);
      instance.set_wifi_tap_name(iface_config.non_bridged_wireless_tap.name);
    } else {
      instance.set_use_bridged_wifi_tap(true);
      instance.set_wifi_tap_name(iface_config.bridged_wireless_tap.name);
    }

    instance.set_ethernet_tap_name(iface_config.ethernet_tap.name);

    instance.set_uuid(FLAGS_uuid);

    instance.set_modem_simulator_host_id(1000 + num);  // Must be 4 digits
    // the deprecated vnc was 6444 + num - 1, and qemu_vnc was vnc - 5900
    instance.set_qemu_vnc_server_port(544 + num - 1);
    instance.set_adb_host_port(6520 + num - 1);
    instance.set_adb_ip_and_port("0.0.0.0:" + std::to_string(6520 + num - 1));

    instance.set_fastboot_host_port(7520 + num - 1);

    std::uint8_t ethernet_mac[6] = {};
    std::uint8_t ethernet_ipv6[16] = {};
    GenerateEthMacForInstance(num - 1, ethernet_mac);
    GenerateCorrespondingIpv6ForMac(ethernet_mac, ethernet_ipv6);
    instance.set_ethernet_mac(MacAddressToString(ethernet_mac));
    instance.set_ethernet_ipv6(Ipv6ToString(ethernet_ipv6));

    instance.set_tombstone_receiver_port(calc_vsock_port(6600));
    instance.set_vehicle_hal_server_port(9300 + num - 1);
    instance.set_audiocontrol_server_port(9410);  /* OK to use the same port number across instances */
    instance.set_config_server_port(calc_vsock_port(6800));

    // gpu related settings
    instance.set_gpu_mode(gpu_mode_vec[instance_index]);
    if (gpu_mode_vec[instance_index] != kGpuModeAuto &&
        gpu_mode_vec[instance_index] != kGpuModeDrmVirgl &&
        gpu_mode_vec[instance_index] != kGpuModeGfxStream &&
        gpu_mode_vec[instance_index] != kGpuModeGuestSwiftshader &&
        gpu_mode_vec[instance_index] != kGpuModeNone) {
      LOG(FATAL) << "Invalid gpu_mode: " << gpu_mode_vec[instance_index];
    }
    if (gpu_mode_vec[instance_index] == kGpuModeAuto) {
      if (ShouldEnableAcceleratedRendering(graphics_availability)) {
        LOG(INFO) << "GPU auto mode: detected prerequisites for accelerated "
            "rendering support.";
        if (vm_manager_vec[0] == QemuManager::name()) {
          LOG(INFO) << "Enabling --gpu_mode=drm_virgl.";
          instance.set_gpu_mode(kGpuModeDrmVirgl);
        } else {
          LOG(INFO) << "Enabling --gpu_mode=gfxstream.";
          instance.set_gpu_mode(kGpuModeGfxStream);
        }
      } else {
        LOG(INFO) << "GPU auto mode: did not detect prerequisites for "
            "accelerated rendering support, enabling "
            "--gpu_mode=guest_swiftshader.";
        instance.set_gpu_mode(kGpuModeGuestSwiftshader);
      }
    } else if (gpu_mode_vec[instance_index] == kGpuModeGfxStream ||
               gpu_mode_vec[instance_index] == kGpuModeDrmVirgl) {
      if (!ShouldEnableAcceleratedRendering(graphics_availability)) {
        LOG(ERROR) << "--gpu_mode="
                   << gpu_mode_vec[instance_index]
                   << " was requested but the prerequisites for accelerated "
                   "rendering were not detected so the device may not "
                   "function correctly. Please consider switching to "
                   "--gpu_mode=auto or --gpu_mode=guest_swiftshader.";
      }
    }

    instance.set_restart_subprocesses(restart_subprocesses_vec[instance_index]);
    instance.set_gpu_capture_binary(gpu_capture_binary_vec[instance_index]);
    if (!gpu_capture_binary_vec[instance_index].empty()) {
      CF_EXPECT(gpu_mode_vec[instance_index] == kGpuModeGfxStream,
          "GPU capture only supported with --gpu_mode=gfxstream");

      // GPU capture runs in a detached mode where the "launcher" process
      // intentionally exits immediately.
      CF_EXPECT(!restart_subprocesses_vec[instance_index],
          "GPU capture only supported with --norestart_subprocesses");
    }

    instance.set_hwcomposer(hwcomposer_vec[instance_index]);
    if (!hwcomposer_vec[instance_index].empty()) {
      if (hwcomposer_vec[instance_index] == kHwComposerRanchu) {
        CF_EXPECT(gpu_mode_vec[instance_index] != kGpuModeDrmVirgl,
            "ranchu hwcomposer not supported with --gpu_mode=drm_virgl");
      }
    }

    if (hwcomposer_vec[instance_index] == kHwComposerAuto) {
      if (gpu_mode_vec[instance_index] == kGpuModeDrmVirgl) {
        instance.set_hwcomposer(kHwComposerDrm);
      } else if (gpu_mode_vec[instance_index] == kGpuModeNone) {
        instance.set_hwcomposer(kHwComposerNone);
      } else {
        instance.set_hwcomposer(kHwComposerRanchu);
      }
    }

    instance.set_enable_gpu_udmabuf(enable_gpu_udmabuf_vec[instance_index]);
    instance.set_enable_gpu_angle(enable_gpu_angle_vec[instance_index]);

    // 1. Keep original code order SetCommandLineOptionWithMode("enable_sandbox")
    // then set_enable_sandbox later.
    // 2. SetCommandLineOptionWithMode condition: if gpu_mode or console,
    // then SetCommandLineOptionWithMode false as original code did,
    // otherwise keep default enable_sandbox value.
    // 3. Sepolicy rules need to be updated to support gpu mode. Temporarily disable
    // auto-enabling sandbox when gpu is enabled (b/152323505).
    default_enable_sandbox += comma_str;
    if ((gpu_mode_vec[instance_index] != kGpuModeGuestSwiftshader) || console_vec[instance_index]) {
      // original code, just moved to each instance setting block
      default_enable_sandbox += "false";
    } else {
      default_enable_sandbox += BoolToString(enable_sandbox_vec[instance_index]);
    }
    comma_str = ",";

    if (!vmm->ConfigureGraphics(const_instance).ok()) {
      LOG(FATAL) << "Invalid (gpu_mode=," << gpu_mode_vec[instance_index] <<
      " hwcomposer= " << hwcomposer_vec[instance_index] <<
      ") does not work with vm_manager=" << vm_manager_vec[0];
    }

    if (gpu_mode_vec[instance_index] != kGpuModeDrmVirgl &&
        gpu_mode_vec[instance_index] != kGpuModeGfxStream) {
      if (vm_manager_vec[0] == QemuManager::name()) {
        instance.set_keyboard_server_port(calc_vsock_port(7000));
        instance.set_touch_server_port(calc_vsock_port(7100));
      }
    }
    // end of gpu related settings

    instance.set_gnss_grpc_proxy_server_port(7200 + num -1);
    instance.set_gnss_file_path(gnss_file_paths[instance_index]);
    instance.set_fixed_location_file_path(fixed_location_file_paths[instance_index]);

    std::vector<std::string> virtual_disk_paths;

    bool os_overlay = true;
    os_overlay &= !protected_vm_vec[instance_index];
    // Gem5 already uses CoW wrappers around disk images
    os_overlay &= vm_manager_vec[0] != Gem5Manager::name();
    os_overlay &= FLAGS_use_overlay;
    if (os_overlay) {
      auto path = const_instance.PerInstancePath("overlay.img");
      virtual_disk_paths.push_back(path);
    } else {
      virtual_disk_paths.push_back(const_instance.os_composite_disk_path());
    }

    bool persistent_disk = true;
    persistent_disk &= !protected_vm_vec[instance_index];
    persistent_disk &= vm_manager_vec[0] != Gem5Manager::name();
    if (persistent_disk) {
      auto path = const_instance.PerInstancePath("persistent_composite.img");
      virtual_disk_paths.push_back(path);
    }

    instance.set_use_sdcard(use_sdcard_vec[instance_index]);

    bool sdcard = true;
    sdcard &= use_sdcard_vec[instance_index];
    sdcard &= !protected_vm_vec[instance_index];
    if (sdcard) {
      virtual_disk_paths.push_back(const_instance.sdcard_path());
    }

    instance.set_virtual_disk_paths(virtual_disk_paths);

    // We'd like to set mac prefix to be 5554, 5555, 5556, ... in normal cases.
    // When --base_instance_num=3, this might be 5556, 5557, 5558, ... (skipping
    // first two)
    instance.set_wifi_mac_prefix(5554 + (num - 1));

    // streaming, webrtc setup
    instance.set_enable_webrtc(start_webrtc_vec[instance_index]);
    instance.set_webrtc_assets_dir(webrtc_assets_dir_vec[instance_index]);

    auto tcp_range  = ParsePortRange(tcp_port_range_vec[instance_index]);
    instance.set_webrtc_tcp_port_range(tcp_range);

    auto udp_range  = ParsePortRange(udp_port_range_vec[instance_index]);
    instance.set_webrtc_udp_port_range(udp_range);

    // end of streaming, webrtc setup

    instance.set_start_webrtc_signaling_server(false);

    CF_EXPECT(Contains(num_to_webrtc_device_id_flag_map, num),
              "Error in looking up num to webrtc_device_id_flag_map");
    instance.set_webrtc_device_id(num_to_webrtc_device_id_flag_map[num]);

    if (!is_first_instance || !start_webrtc_vec[instance_index]) {
      // Only the first instance starts the signaling server or proxy
      instance.set_start_webrtc_signaling_server(false);
      instance.set_start_webrtc_sig_server_proxy(false);
    } else {
      auto port = 8443 + num - 1;
      // Change the signaling server port for all instances
      tmp_config_obj.set_sig_server_port(port);
      // Either the signaling server or the proxy is started, never both
      instance.set_start_webrtc_signaling_server(FLAGS_start_webrtc_sig_server);
      // The proxy is only started if the host operator is available
      instance.set_start_webrtc_sig_server_proxy(
          cuttlefish::FileIsSocket(HOST_OPERATOR_SOCKET_PATH) &&
          !FLAGS_start_webrtc_sig_server);
    }

    // Start wmediumd process for the first instance if
    // vhost_user_mac80211_hwsim is not specified.
    const bool start_wmediumd = enforce_mac80211_hwsim &&
                                FLAGS_vhost_user_mac80211_hwsim.empty() &&
                                is_first_instance;
    if (start_wmediumd) {
      // TODO(b/199020470) move this to the directory for shared resources
      auto vhost_user_socket_path =
          const_instance.PerInstanceInternalPath("vhost_user_mac80211");
      auto wmediumd_api_socket_path =
          const_instance.PerInstanceInternalPath("wmediumd_api_server");

      tmp_config_obj.set_vhost_user_mac80211_hwsim(vhost_user_socket_path);
      tmp_config_obj.set_wmediumd_api_server_socket(wmediumd_api_socket_path);
      instance.set_start_wmediumd(true);
    } else {
      instance.set_start_wmediumd(false);
    }

    instance.set_start_netsim(is_first_instance && is_any_netsim);

    instance.set_start_rootcanal(is_first_instance && !is_bt_netsim &&
                                 (FLAGS_rootcanal_instance_num <= 0));

    if (!FLAGS_ap_rootfs_image.empty() && !FLAGS_ap_kernel_image.empty() && start_wmediumd) {
      // TODO(264537774): Ubuntu grub modules / grub monoliths cannot be used to boot
      // 64 bit kernel using 32 bit u-boot / grub.
      // Enable this code back after making sure it works across all popular environments
      // if (CanGenerateEsp(guest_configs[0].target_arch)) {
      //   instance.set_ap_boot_flow(CuttlefishConfig::InstanceSpecific::APBootFlow::Grub);
      // } else {
      //   instance.set_ap_boot_flow(CuttlefishConfig::InstanceSpecific::APBootFlow::LegacyDirect);
      // }
      instance.set_ap_boot_flow(CuttlefishConfig::InstanceSpecific::APBootFlow::LegacyDirect);
    } else {
      instance.set_ap_boot_flow(CuttlefishConfig::InstanceSpecific::APBootFlow::None);
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

  // After SetCommandLineOptionWithMode,
  // default flag values changed, need recalculate name_to_default_value
  name_to_default_value = CurrentFlagsToDefaultValue();
  // After last SetCommandLineOptionWithMode, we could set these special flags
  enable_sandbox_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      enable_sandbox));

  instance_index = 0;
  for (const auto& num : instance_nums) {
    auto instance = tmp_config_obj.ForInstance(num);
    instance.set_enable_sandbox(enable_sandbox_vec[instance_index]);
    instance_index++;
  }

  DiskImageFlagsVectorization(tmp_config_obj, fetcher_config);

  return tmp_config_obj;
}

Result<void> SetDefaultFlagsForQemu(Arch target_arch, std::map<std::string, std::string>& name_to_default_value) {
  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());
  int32_t instances_size = instance_nums.size();
  std::vector<std::string> gpu_mode_vec =
      CF_EXPECT(GET_FLAG_STR_VALUE(gpu_mode));
  std::vector<bool> start_webrtc_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      start_webrtc));
  std::string default_start_webrtc = "";

  for (int instance_index = 0; instance_index < instance_nums.size(); instance_index++) {
    if (instance_index > 0) {
      default_start_webrtc += ",";
    }
    if (gpu_mode_vec[instance_index] == kGpuModeGuestSwiftshader && !start_webrtc_vec[instance_index]) {
      // This makes WebRTC the default streamer unless the user requests
      // another via a --star_<streamer> flag, while at the same time it's
      // possible to run without any streamer by setting --start_webrtc=false.
      default_start_webrtc += "true";
    } else {
      default_start_webrtc += BoolToString(start_webrtc_vec[instance_index]);
    }
  }
  // This is the 1st place to set "start_webrtc" flag value
  // for now, we don't set non-default options for QEMU
  SetCommandLineOptionWithMode("start_webrtc", default_start_webrtc.c_str(),
                               SET_FLAGS_DEFAULT);

  std::string default_bootloader =
      DefaultHostArtifactsPath("etc/bootloader_");
  if(target_arch == Arch::Arm) {
      // Bootloader is unstable >512MB RAM on 32-bit ARM
      SetCommandLineOptionWithMode("memory_mb", "512", SET_FLAGS_VALUE);
      default_bootloader += "arm";
  } else if (target_arch == Arch::Arm64) {
      default_bootloader += "aarch64";
  } else if (target_arch == Arch::RiscV64) {
      default_bootloader += "riscv64";
  } else {
      default_bootloader += "x86_64";
  }
  default_bootloader += "/bootloader.qemu";
  SetCommandLineOptionWithMode("bootloader", default_bootloader.c_str(),
                               SET_FLAGS_DEFAULT);
  return {};
}


Result<void> SetDefaultFlagsForCrosvm(
    const std::vector<GuestConfig>& guest_configs,
    std::map<std::string, std::string>& name_to_default_value) {
  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());
  int32_t instances_size = instance_nums.size();
  std::vector<bool> start_webrtc_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
      start_webrtc));
  std::string default_start_webrtc = "";

  std::set<Arch> supported_archs{Arch::X86_64};
  bool default_enable_sandbox =
      supported_archs.find(HostArch()) != supported_archs.end() &&
      EnsureDirectoryExists(kCrosvmVarEmptyDir).ok() &&
      IsDirectoryEmpty(kCrosvmVarEmptyDir) && !IsRunningInContainer();

  std::vector<std::string> system_image_dir =
      android::base::Split(FLAGS_system_image_dir, ",");
  std::string cur_bootloader = "";
  std::string default_bootloader = "";
  std::string default_enable_sandbox_str = "";
  for (int instance_index = 0; instance_index < instance_nums.size(); instance_index++) {
    if (guest_configs[instance_index].android_version_number == "11.0.0") {
      cur_bootloader = DefaultHostArtifactsPath("etc/bootloader_");
      if (guest_configs[instance_index].target_arch == Arch::Arm64) {
        cur_bootloader += "aarch64";
      } else {
        cur_bootloader += "x86_64";
      }
      cur_bootloader += "/bootloader.crosvm";
    } else {
      if (instance_index >= system_image_dir.size()) {
        cur_bootloader = system_image_dir[0];
      } else {
        cur_bootloader = system_image_dir[instance_index];
      }
      cur_bootloader += "/bootloader";
    }
    if (instance_index > 0) {
      default_bootloader += ",";
      default_enable_sandbox_str += ",";
      default_start_webrtc += ",";
    }
    default_bootloader += cur_bootloader;
    default_enable_sandbox_str += BoolToString(default_enable_sandbox);
    if (!start_webrtc_vec[instance_index]) {
      // This makes WebRTC the default streamer unless the user requests
      // another via a --star_<streamer> flag, while at the same time it's
      // possible to run without any streamer by setting --start_webrtc=false.
      default_start_webrtc += "true";
    } else {
      default_start_webrtc += BoolToString(start_webrtc_vec[instance_index]);
    }
  }
  SetCommandLineOptionWithMode("bootloader", default_bootloader.c_str(),
                               SET_FLAGS_DEFAULT);
  // This is the 1st place to set "start_webrtc" flag value
  SetCommandLineOptionWithMode("start_webrtc", default_start_webrtc.c_str(),
                               SET_FLAGS_DEFAULT);
  // This is the 1st place to set "enable_sandbox" flag value
  SetCommandLineOptionWithMode("enable_sandbox",
                               default_enable_sandbox_str.c_str(), SET_FLAGS_DEFAULT);
  return {};
}

void SetDefaultFlagsForGem5() {
  // TODO: Add support for gem5 gpu models
  SetCommandLineOptionWithMode("gpu_mode", kGpuModeGuestSwiftshader,
                               SET_FLAGS_DEFAULT);

  SetCommandLineOptionWithMode("cpus", "1", SET_FLAGS_DEFAULT);
}

void SetDefaultFlagsForOpenwrt(Arch target_arch) {
  if (target_arch == Arch::X86_64) {
    SetCommandLineOptionWithMode(
        "ap_kernel_image",
        DefaultHostArtifactsPath("etc/openwrt/images/openwrt_kernel_x86_64")
            .c_str(),
        SET_FLAGS_DEFAULT);
    SetCommandLineOptionWithMode(
        "ap_rootfs_image",
        DefaultHostArtifactsPath("etc/openwrt/images/openwrt_rootfs_x86_64")
            .c_str(),
        SET_FLAGS_DEFAULT);
  } else if (target_arch == Arch::Arm64) {
    SetCommandLineOptionWithMode(
        "ap_kernel_image",
        DefaultHostArtifactsPath("etc/openwrt/images/openwrt_kernel_aarch64")
            .c_str(),
        SET_FLAGS_DEFAULT);
    SetCommandLineOptionWithMode(
        "ap_rootfs_image",
        DefaultHostArtifactsPath("etc/openwrt/images/openwrt_rootfs_aarch64")
            .c_str(),
        SET_FLAGS_DEFAULT);
  }
}

Result<std::vector<GuestConfig>> GetGuestConfigAndSetDefaults() {
  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());
  int32_t instances_size = instance_nums.size();
  CF_EXPECT(ResolveInstanceFiles(), "Failed to resolve instance files");

  std::vector<GuestConfig> guest_configs = CF_EXPECT(ReadGuestConfig());

  // TODO(weihsu), b/250988697:
  // assume all instances are using same VM manager/app/arch,
  // later that multiple instances may use different VM manager/app/arch

  // Temporary add this checking to make sure all instances have same target_arch.
  // This checking should be removed later.
  for (int instance_index = 1; instance_index < guest_configs.size(); instance_index++) {
    CF_EXPECT(guest_configs[0].target_arch == guest_configs[instance_index].target_arch,
              "all instance target_arch should be same");
  }
  if (FLAGS_vm_manager == "") {
    if (IsHostCompatible(guest_configs[0].target_arch)) {
      FLAGS_vm_manager = CrosvmManager::name();
    } else {
      FLAGS_vm_manager = QemuManager::name();
    }
  }
  // TODO(weihsu), b/250988697:
  // Currently, all instances should use same vmm
  std::vector<std::string> vm_manager_vec =
      android::base::Split(FLAGS_vm_manager, ",");
  // get flag default values and store into map
  auto name_to_default_value = CurrentFlagsToDefaultValue();

  if (vm_manager_vec[0] == QemuManager::name()) {

    CF_EXPECT(SetDefaultFlagsForQemu(guest_configs[0].target_arch, name_to_default_value));
  } else if (vm_manager_vec[0] == CrosvmManager::name()) {
    CF_EXPECT(SetDefaultFlagsForCrosvm(guest_configs, name_to_default_value));
  } else if (vm_manager_vec[0] == Gem5Manager::name()) {
    // TODO: Get the other architectures working
    if (guest_configs[0].target_arch != Arch::Arm64) {
      return CF_ERR("Gem5 only supports ARM64");
    }
    SetDefaultFlagsForGem5();
  } else {
    return CF_ERR("Unknown Virtual Machine Manager: " << FLAGS_vm_manager);
  }
  if (vm_manager_vec[0] != Gem5Manager::name()) {
    // After SetCommandLineOptionWithMode in SetDefaultFlagsForCrosvm/Qemu,
    // default flag values changed, need recalculate name_to_default_value
    name_to_default_value = CurrentFlagsToDefaultValue();
    std::vector<bool> start_webrtc_vec = CF_EXPECT(GET_FLAG_BOOL_VALUE(
        start_webrtc));
    bool start_webrtc = false;
    for(bool value : start_webrtc_vec) {
      start_webrtc |= value;
    }

    auto host_operator_present =
        cuttlefish::FileIsSocket(HOST_OPERATOR_SOCKET_PATH);
    // The default for starting signaling server depends on whether or not webrtc
    // is to be started and the presence of the host orchestrator.
    SetCommandLineOptionWithMode(
        "start_webrtc_sig_server",
        start_webrtc && !host_operator_present ? "true" : "false",
        SET_FLAGS_DEFAULT);
    SetCommandLineOptionWithMode(
        "webrtc_sig_server_addr",
        host_operator_present ? HOST_OPERATOR_SOCKET_PATH : "0.0.0.0",
        SET_FLAGS_DEFAULT);
  }

  SetDefaultFlagsForOpenwrt(guest_configs[0].target_arch);

  // Set the env variable to empty (in case the caller passed a value for it).
  unsetenv(kCuttlefishConfigEnvVarName);

  return guest_configs;
}

std::string GetConfigFilePath(const CuttlefishConfig& config) {
  return config.AssemblyPath("cuttlefish_config.json");
}

std::string GetCuttlefishEnvPath() {
  return StringFromEnv("HOME", ".") + "/.cuttlefish.sh";
}

std::string GetSeccompPolicyDir() {
  static const std::string kSeccompDir = std::string("usr/share/crosvm/") +
                                         cuttlefish::HostArchStr() +
                                         "-linux-gnu/seccomp";
  return DefaultHostArtifactsPath(kSeccompDir);
}

} // namespace cuttlefish
