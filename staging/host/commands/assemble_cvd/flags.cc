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

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "flags.h"
#include "flags_defaults.h"
#include "host/commands/assemble_cvd/alloc.h"
#include "host/commands/assemble_cvd/boot_config.h"
#include "host/commands/assemble_cvd/disk_flags.h"
#include "host/libs/config/config_flag.h"
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

DEFINE_string(cpus, std::to_string(CF_DEFAULTS_CPUS),
              "Virtual CPU count.");
DEFINE_string(data_policy, CF_DEFAULTS_DATA_POLICY,
              "How to handle userdata partition."
              " Either 'use_existing', 'create_if_missing', 'resize_up_to', or "
              "'always_create'.");
DEFINE_string(blank_data_image_mb,
              std::to_string(CF_DEFAULTS_BLANK_DATA_IMAGE_MB),
             "The size of the blank data image to generate, MB.");
DEFINE_string(gdb_port, std::to_string(CF_DEFAULTS_GDB_PORT),
             "Port number to spawn kernel gdb on e.g. -gdb_port=1234. The"
             "kernel must have been built with CONFIG_RANDOMIZE_BASE "
             "disabled.");

constexpr const char kDisplayHelp[] =
    "Comma separated key=value pairs of display properties. Supported "
    "properties:\n"
    " 'width': required, width of the display in pixels\n"
    " 'height': required, height of the display in pixels\n"
    " 'dpi': optional, default 320, density of the display\n"
    " 'refresh_rate_hz': optional, default 60, display refresh rate in Hertz\n"
    ". Example usage: \n"
    "--display0=width=1280,height=720\n"
    "--display1=width=1440,height=900,dpi=480,refresh_rate_hz=30\n";

// TODO(b/192495477): combine these into a single repeatable '--display' flag
// when assemble_cvd switches to using the new flag parsing library.
DEFINE_string(display0, CF_DEFAULTS_DISPLAY0, kDisplayHelp);
DEFINE_string(display1, CF_DEFAULTS_DISPLAY1, kDisplayHelp);
DEFINE_string(display2, CF_DEFAULTS_DISPLAY2, kDisplayHelp);
DEFINE_string(display3, CF_DEFAULTS_DISPLAY3, kDisplayHelp);

// TODO(b/171305898): mark these as deprecated after multi-display is fully
// enabled.
DEFINE_string(x_res, "0", "Width of the screen in pixels");
DEFINE_string(y_res, "0", "Height of the screen in pixels");
DEFINE_string(dpi, "0", "Pixels per inch for the screen");
DEFINE_string(refresh_rate_hz, "60", "Screen refresh rate in Hertz");
DEFINE_string(kernel_path, CF_DEFAULTS_KERNEL_PATH,
              "Path to the kernel. Overrides the one from the boot image");
DEFINE_string(initramfs_path, CF_DEFAULTS_INITRAMFS_PATH,
              "Path to the initramfs");
DEFINE_string(extra_kernel_cmdline, CF_DEFAULTS_EXTRA_KERNEL_CMDLINE,
              "Additional flags to put on the kernel command line");
DEFINE_string(extra_bootconfig_args, CF_DEFAULTS_EXTRA_BOOTCONFIG_ARGS,
              "Space-separated list of extra bootconfig args. "
              "Note: overwriting an existing bootconfig argument "
              "requires ':=' instead of '='.");
DEFINE_string(guest_enforce_security,
              CF_DEFAULTS_GUEST_ENFORCE_SECURITY?"true":"false",
            "Whether to run in enforcing mode (non permissive).");
DEFINE_string(memory_mb, std::to_string(CF_DEFAULTS_MEMORY_MB),
             "Total amount of memory available for guest, MB.");
DEFINE_string(serial_number, CF_DEFAULTS_SERIAL_NUMBER,
              "Serial number to use for the device");
DEFINE_string(use_random_serial, CF_DEFAULTS_USE_RANDOM_SERIAL?"true":"false",
            "Whether to use random serial for the device.");
DEFINE_string(vm_manager, CF_DEFAULTS_VM_MANAGER,
              "What virtual machine manager to use, one of {qemu_cli, crosvm}");
DEFINE_string(gpu_mode, CF_DEFAULTS_GPU_MODE,
              "What gpu configuration to use, one of {auto, drm_virgl, "
              "gfxstream, guest_swiftshader}");
DEFINE_string(hwcomposer, CF_DEFAULTS_HWCOMPOSER,
              "What hardware composer to use, one of {auto, drm, ranchu} ");
DEFINE_string(gpu_capture_binary, CF_DEFAULTS_GPU_CAPTURE_BINARY,
              "Path to the GPU capture binary to use when capturing GPU traces"
              "(ngfx, renderdoc, etc)");
DEFINE_bool(enable_gpu_udmabuf, CF_DEFAULTS_ENABLE_GPU_UDMABUF,
            "Use the udmabuf driver for zero-copy virtio-gpu");

DEFINE_bool(enable_gpu_angle, CF_DEFAULTS_ENABLE_GPU_ANGLE,
            "Use ANGLE to provide GLES implementation (always true for"
            " guest_swiftshader");
DEFINE_bool(deprecated_boot_completed, CF_DEFAULTS_DEPRECATED_BOOT_COMPLETED,
            "Log boot completed message to"
            " host kernel. This is only used during transition of our clients."
            " Will be deprecated soon.");

DEFINE_string(use_allocd, CF_DEFAULTS_USE_ALLOCD?"true":"false",
            "Acquire static resources from the resource allocator daemon.");
DEFINE_string(
    enable_minimal_mode, CF_DEFAULTS_ENABLE_MINIMAL_MODE ? "true" : "false",
    "Only enable the minimum features to boot a cuttlefish device and "
    "support minimal UI interactions.\nNote: Currently only supports "
    "handheld/phone targets");
DEFINE_string(
    pause_in_bootloader, CF_DEFAULTS_PAUSE_IN_BOOTLOADER?"true":"false",
    "Stop the bootflow in u-boot. You can continue the boot by connecting "
    "to the device console and typing in \"boot\".");
DEFINE_bool(enable_host_bluetooth, CF_DEFAULTS_ENABLE_HOST_BLUETOOTH,
            "Enable the root-canal which is Bluetooth emulator in the host.");
DEFINE_bool(rootcanal_attach_mode, CF_DEFAULTS_ROOTCANAL_ATTACH_MODE,
            "[DEPRECATED] Ignored, use rootcanal_instance_num instead");
DEFINE_int32(
    rootcanal_instance_num, CF_DEFAULTS_ENABLE_ROOTCANAL_INSTANCE_NUM,
    "If it is greater than 0, use an existing rootcanal instance which is "
    "launched from cuttlefish instance "
    "with rootcanal_instance_num. Else, launch a new rootcanal instance");
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
DEFINE_bool(
    enable_sandbox, CF_DEFAULTS_ENABLE_SANDBOX,
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

DEFINE_bool(start_webrtc, CF_DEFAULTS_START_WEBRTC,
            "Whether to start the webrtc process.");

DEFINE_string(webrtc_assets_dir, CF_DEFAULTS_WEBRTC_ASSETS_DIR,
              "[Experimental] Path to WebRTC webpage assets.");

DEFINE_string(webrtc_certs_dir, CF_DEFAULTS_WEBRTC_CERTS_DIR,
              "[Experimental] Path to WebRTC certificates directory.");

DEFINE_string(webrtc_public_ip, CF_DEFAULTS_WEBRTC_PUBLIC_IP,
              "[Deprecated] Ignored, webrtc can figure out its IP address");

DEFINE_bool(webrtc_enable_adb_websocket,
            CF_DEFAULTS_WEBRTC_ENABLE_ADB_WEBSOCKET,
            "[Experimental] If enabled, exposes local adb service through a "
            "websocket.");

static constexpr auto HOST_OPERATOR_SOCKET_PATH = "/run/cuttlefish/operator";

DEFINE_bool(
    // The actual default for this flag is set with SetCommandLineOption() in
    // GetKernelConfigsAndSetDefaults() at the end of this file.
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
DEFINE_string(tcp_port_range, CF_DEFAULTS_TCP_PORT_RANGE,
              "The minimum and maximum TCP port numbers to allocate for ICE "
              "candidates as 'min:max'. To use any port just specify '0:0'");

DEFINE_string(udp_port_range, CF_DEFAULTS_UDP_PORT_RANGE,
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

DEFINE_string(sig_server_headers_file, CF_DEFAULTS_SIG_SERVER_HEADERS_FILE,
              "Path to a file containing HTTP headers to be included in the "
              "connection to the signaling server. Each header should be on a "
              "line by itself in the form <name>: <value>");

DEFINE_string(
    webrtc_device_id, CF_DEFAULTS_WEBRTC_DEVICE_ID,
    "The for the device to register with the signaling server. Every "
    "appearance of the substring '{num}' in the device id will be substituted "
    "with the instance number to support multiple instances");

DEFINE_string(uuid, CF_DEFAULTS_UUID,
              "UUID to use for the device. Random if not specified");
DEFINE_string(daemon, CF_DEFAULTS_DAEMON?"true":"false",
            "Run cuttlefish in background, the launcher exits on boot "
            "completed/failed");

DEFINE_string(setupwizard_mode, CF_DEFAULTS_SETUPWIZARD_MODE,
              "One of DISABLED,OPTIONAL,REQUIRED");
DEFINE_bool(enable_bootanimation, CF_DEFAULTS_ENABLE_BOOTANIMATION,
            "Whether to enable the boot animation.");

DEFINE_string(qemu_binary_dir, CF_DEFAULTS_QEMU_BINARY_DIR,
              "Path to the directory containing the qemu binary to use");
DEFINE_string(crosvm_binary, CF_DEFAULTS_CROSVM_BINARY,
              "The Crosvm binary to use");
DEFINE_string(gem5_binary_dir, CF_DEFAULTS_GEM5_BINARY_DIR,
              "Path to the gem5 build tree root");
DEFINE_string(gem5_checkpoint_dir, CF_DEFAULTS_GEM5_CHECKPOINT_DIR,
              "Path to the gem5 restore checkpoint directory");
DEFINE_string(gem5_debug_file, CF_DEFAULTS_GEM5_DEBUG_FILE,
              "The file name where gem5 saves debug prints and logs");
DEFINE_string(gem5_debug_flags, CF_DEFAULTS_GEM5_DEBUG_FLAGS,
              "The debug flags gem5 uses to print debugs to file");

DEFINE_bool(restart_subprocesses, CF_DEFAULTS_RESTART_SUBPROCESSES,
            "Restart any crashed host process");
DEFINE_bool(enable_vehicle_hal_grpc_server,
            CF_DEFAULTS_ENABLE_VEHICLE_HAL_GRPC_SERVER,
            "Enables the vehicle HAL "
            "emulation gRPC server on the host");
DEFINE_string(bootloader, CF_DEFAULTS_BOOTLOADER, "Bootloader binary path");
DEFINE_string(boot_slot, CF_DEFAULTS_BOOT_SLOT,
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
DEFINE_string(ril_dns, CF_DEFAULTS_RIL_DNS,
              "DNS address of mobile network (RIL)");
DEFINE_bool(kgdb, CF_DEFAULTS_KGDB,
            "Configure the virtual device for debugging the kernel "
            "with kgdb/kdb. The kernel must have been built with "
            "kgdb support, and serial console must be enabled.");

DEFINE_bool(start_gnss_proxy, CF_DEFAULTS_START_GNSS_PROXY,
            "Whether to start the gnss proxy.");

DEFINE_string(gnss_file_path, CF_DEFAULTS_GNSS_FILE_PATH,
              "Local gnss raw measurement file path for the gnss proxy");

DEFINE_string(fixed_location_file_path, CF_DEFAULTS_FIXED_LOCATION_FILE_PATH,
              "Local fixed location file path for the gnss proxy");

// by default, this modem-simulator is disabled
DEFINE_string(enable_modem_simulator,
              CF_DEFAULTS_ENABLE_MODEM_SIMULATOR ? "true" : "false",
              "Enable the modem simulator to process RILD AT commands");
// modem_simulator_sim_type=2 for test CtsCarrierApiTestCases
DEFINE_string(modem_simulator_sim_type,
              std::to_string(CF_DEFAULTS_MODEM_SIMULATOR_SIM_TYPE),
              "Sim type: 1 for normal, 2 for CtsCarrierApiTestCases");

DEFINE_bool(console, CF_DEFAULTS_CONSOLE, "Enable the serial console");

DEFINE_bool(enable_kernel_log, CF_DEFAULTS_ENABLE_KERNEL_LOG,
            "Enable kernel console/dmesg logging");

DEFINE_bool(vhost_net, CF_DEFAULTS_VHOST_NET,
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

DEFINE_bool(record_screen, CF_DEFAULTS_RECORD_SCREEN,
            "Enable screen recording. "
            "Requires --start_webrtc");

DEFINE_bool(smt, CF_DEFAULTS_SMT,
            "Enable simultaneous multithreading (SMT/HT)");

DEFINE_string(
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

DEFINE_string(use_sdcard, CF_DEFAULTS_USE_SDCARD?"true":"false",
            "Create blank SD-Card image and expose to guest");

DEFINE_bool(protected_vm, CF_DEFAULTS_PROTECTED_VM,
            "Boot in Protected VM mode");

DEFINE_bool(enable_audio, CF_DEFAULTS_ENABLE_AUDIO,
            "Whether to play or capture audio");

DEFINE_string(camera_server_port, std::to_string(CF_DEFAULTS_CAMERA_SERVER_PORT),
              "camera vsock port");

DEFINE_string(userdata_format, CF_DEFAULTS_USERDATA_FORMAT,
              "The userdata filesystem format");

DEFINE_bool(use_overlay, CF_DEFAULTS_USE_OVERLAY,
            "Capture disk writes an overlay. This is a "
            "prerequisite for powerwash_cvd or multiple instances.");

DEFINE_string(modem_simulator_count,
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

std::optional<CuttlefishConfig::DisplayConfig> ParseDisplayConfig(
    const std::string& flag) {
  if (flag.empty()) {
    return std::nullopt;
  }

  std::unordered_map<std::string, std::string> props;

  const std::vector<std::string> pairs = android::base::Split(flag, ",");
  for (const std::string& pair : pairs) {
    const std::vector<std::string> keyvalue = android::base::Split(pair, "=");
    CHECK_EQ(2, keyvalue.size()) << "Invalid display: " << flag;

    const std::string& prop_key = keyvalue[0];
    const std::string& prop_val = keyvalue[1];
    props[prop_key] = prop_val;
  }

  CHECK(props.find("width") != props.end())
      << "Display configuration missing 'width' in " << flag;
  CHECK(props.find("height") != props.end())
      << "Display configuration missing 'height' in " << flag;

  int display_width;
  CHECK(android::base::ParseInt(props["width"], &display_width))
      << "Display configuration invalid 'width' in " << flag;

  int display_height;
  CHECK(android::base::ParseInt(props["height"], &display_height))
      << "Display configuration invalid 'height' in " << flag;

  int display_dpi = 320;
  auto display_dpi_it = props.find("dpi");
  if (display_dpi_it != props.end()) {
    CHECK(android::base::ParseInt(display_dpi_it->second, &display_dpi))
        << "Display configuration invalid 'dpi' in " << flag;
  }

  int display_refresh_rate_hz = 60;
  auto display_refresh_rate_hz_it = props.find("refresh_rate_hz");
  if (display_refresh_rate_hz_it != props.end()) {
    CHECK(android::base::ParseInt(display_refresh_rate_hz_it->second,
                                  &display_refresh_rate_hz))
        << "Display configuration invalid 'refresh_rate_hz' in " << flag;
  }

  return CuttlefishConfig::DisplayConfig{
      .width = display_width,
      .height = display_height,
      .dpi = display_dpi,
      .refresh_rate_hz = display_refresh_rate_hz,
  };
}

#ifdef __ANDROID__
Result<std::vector<KernelConfig>> ReadKernelConfig() {
  std::vector<KernelConfig> rets;
  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());
  for (int instance_index = 0; instance_index < instance_nums.size(); instance_index++) {
    // QEMU isn't on Android, so always follow host arch
    KernelConfig ret{};
    ret.target_arch = HostArch();
    ret.bootconfig_supported = true;
    rets.push_back(ret);
  }
  return rets;
}
#else
Result<std::vector<KernelConfig>> ReadKernelConfig() {
  std::vector<KernelConfig> kernel_configs;
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

    KernelConfig kernel_config;
    if (config.find("\nCONFIG_ARM=y") != std::string::npos) {
      kernel_config.target_arch = Arch::Arm;
    } else if (config.find("\nCONFIG_ARM64=y") != std::string::npos) {
      kernel_config.target_arch = Arch::Arm64;
    } else if (config.find("\nCONFIG_X86_64=y") != std::string::npos) {
      kernel_config.target_arch = Arch::X86_64;
    } else if (config.find("\nCONFIG_X86=y") != std::string::npos) {
      kernel_config.target_arch = Arch::X86;
    } else {
      return CF_ERR("Unknown target architecture");
    }
    kernel_config.bootconfig_supported =
        config.find("\nCONFIG_BOOT_CONFIG=y") != std::string::npos;
    // Once all Cuttlefish kernel versions are at least 5.15, this code can be
    // removed. CONFIG_CRYPTO_HCTR2=y will always be set.
    kernel_config.hctr2_supported =
        config.find("\nCONFIG_CRYPTO_HCTR2=y") != std::string::npos;

    unlink(ikconfig_path.c_str());
    kernel_configs.push_back(kernel_config);
  }
  return kernel_configs;
}

#endif  // #ifdef __ANDROID__

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

} // namespace

Result<CuttlefishConfig> InitializeCuttlefishConfiguration(
    const std::string& root_dir,
    const std::vector<KernelConfig>& kernel_configs,
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

  // TODO(weihsu), b/250988697: these should move to instance,
  // currently use instance[0] to setup for all instances
  tmp_config_obj.set_bootconfig_supported(kernel_configs[0].bootconfig_supported);
  tmp_config_obj.set_filename_encryption_mode(
      kernel_configs[0].hctr2_supported ? "hctr2" : "cts");
  auto vmm = GetVmManager(vm_manager_vec[0], kernel_configs[0].target_arch);
  if (!vmm) {
    LOG(FATAL) << "Invalid vm_manager: " << vm_manager_vec[0];
  }
  tmp_config_obj.set_vm_manager(vm_manager_vec[0]);

  const GraphicsAvailability graphics_availability =
    GetGraphicsAvailabilityWithSubprocessCheck();

  LOG(DEBUG) << graphics_availability;

  tmp_config_obj.set_gpu_mode(FLAGS_gpu_mode);
  if (tmp_config_obj.gpu_mode() != kGpuModeAuto &&
      tmp_config_obj.gpu_mode() != kGpuModeDrmVirgl &&
      tmp_config_obj.gpu_mode() != kGpuModeGfxStream &&
      tmp_config_obj.gpu_mode() != kGpuModeGuestSwiftshader) {
    LOG(FATAL) << "Invalid gpu_mode: " << FLAGS_gpu_mode;
  }
  if (tmp_config_obj.gpu_mode() == kGpuModeAuto) {
    if (ShouldEnableAcceleratedRendering(graphics_availability)) {
      LOG(INFO) << "GPU auto mode: detected prerequisites for accelerated "
                   "rendering support.";
      if (vm_manager_vec[0] == QemuManager::name()) {
        LOG(INFO) << "Enabling --gpu_mode=drm_virgl.";
        tmp_config_obj.set_gpu_mode(kGpuModeDrmVirgl);
      } else {
        LOG(INFO) << "Enabling --gpu_mode=gfxstream.";
        tmp_config_obj.set_gpu_mode(kGpuModeGfxStream);
      }
    } else {
      LOG(INFO) << "GPU auto mode: did not detect prerequisites for "
                   "accelerated rendering support, enabling "
                   "--gpu_mode=guest_swiftshader.";
      tmp_config_obj.set_gpu_mode(kGpuModeGuestSwiftshader);
    }
  } else if (tmp_config_obj.gpu_mode() == kGpuModeGfxStream ||
             tmp_config_obj.gpu_mode() == kGpuModeDrmVirgl) {
    if (!ShouldEnableAcceleratedRendering(graphics_availability)) {
      LOG(ERROR) << "--gpu_mode="
                 << tmp_config_obj.gpu_mode()
                 << " was requested but the prerequisites for accelerated "
                    "rendering were not detected so the device may not "
                    "function correctly. Please consider switching to "
                    "--gpu_mode=auto or --gpu_mode=guest_swiftshader.";
    }
  }

  tmp_config_obj.set_restart_subprocesses(FLAGS_restart_subprocesses);
  tmp_config_obj.set_gpu_capture_binary(FLAGS_gpu_capture_binary);
  if (!tmp_config_obj.gpu_capture_binary().empty()) {
    CHECK(tmp_config_obj.gpu_mode() == kGpuModeGfxStream)
        << "GPU capture only supported with --gpu_mode=gfxstream";

    // GPU capture runs in a detached mode where the "launcher" process
    // intentionally exits immediately.
    CHECK(!tmp_config_obj.restart_subprocesses())
        << "GPU capture only supported with --norestart_subprocesses";
  }

  tmp_config_obj.set_hwcomposer(FLAGS_hwcomposer);
  if (!tmp_config_obj.hwcomposer().empty()) {
    if (tmp_config_obj.hwcomposer() == kHwComposerRanchu) {
      CHECK(tmp_config_obj.gpu_mode() != kGpuModeDrmVirgl)
        << "ranchu hwcomposer not supported with --gpu_mode=drm_virgl";
    }
  }

  if (tmp_config_obj.hwcomposer() == kHwComposerAuto) {
      if (tmp_config_obj.gpu_mode() == kGpuModeDrmVirgl) {
        tmp_config_obj.set_hwcomposer(kHwComposerDrm);
      } else {
        tmp_config_obj.set_hwcomposer(kHwComposerRanchu);
      }
  }

  tmp_config_obj.set_enable_gpu_udmabuf(FLAGS_enable_gpu_udmabuf);
  tmp_config_obj.set_enable_gpu_angle(FLAGS_enable_gpu_angle);

  // Sepolicy rules need to be updated to support gpu mode. Temporarily disable
  // auto-enabling sandbox when gpu is enabled (b/152323505).
  if (tmp_config_obj.gpu_mode() != kGpuModeGuestSwiftshader) {
    SetCommandLineOptionWithMode("enable_sandbox", "false", SET_FLAGS_DEFAULT);
  }

  if (vmm->ConfigureGraphics(tmp_config_obj).empty()) {
    LOG(FATAL) << "Invalid (gpu_mode=," << FLAGS_gpu_mode <<
               " hwcomposer= " << FLAGS_hwcomposer <<
               ") does not work with vm_manager=" << vm_manager_vec[0];
  }

  tmp_config_obj.set_enable_bootanimation(FLAGS_enable_bootanimation);

  auto secure_hals = android::base::Split(FLAGS_secure_hals, ",");
  tmp_config_obj.set_secure_hals(
      std::set<std::string>(secure_hals.begin(), secure_hals.end()));

  tmp_config_obj.set_extra_kernel_cmdline(FLAGS_extra_kernel_cmdline);
  tmp_config_obj.set_extra_bootconfig_args(FLAGS_extra_bootconfig_args);

  if (FLAGS_console) {
    SetCommandLineOptionWithMode("enable_sandbox", "false", SET_FLAGS_DEFAULT);
  }

  tmp_config_obj.set_enable_kernel_log(FLAGS_enable_kernel_log);

  tmp_config_obj.set_host_tools_version(HostToolsCrc());

  tmp_config_obj.set_deprecated_boot_completed(FLAGS_deprecated_boot_completed);

  tmp_config_obj.set_qemu_binary_dir(FLAGS_qemu_binary_dir);
  tmp_config_obj.set_crosvm_binary(FLAGS_crosvm_binary);
  tmp_config_obj.set_gem5_debug_flags(FLAGS_gem5_debug_flags);
  tmp_config_obj.set_gem5_debug_file(FLAGS_gem5_debug_file);

  tmp_config_obj.set_seccomp_policy_dir(FLAGS_seccomp_policy_dir);

  tmp_config_obj.set_enable_webrtc(FLAGS_start_webrtc);
  tmp_config_obj.set_webrtc_assets_dir(FLAGS_webrtc_assets_dir);
  tmp_config_obj.set_webrtc_certs_dir(FLAGS_webrtc_certs_dir);
  tmp_config_obj.set_sig_server_secure(FLAGS_webrtc_sig_server_secure);
  // Note: This will be overridden if the sig server is started by us
  tmp_config_obj.set_sig_server_port(FLAGS_webrtc_sig_server_port);
  tmp_config_obj.set_sig_server_address(FLAGS_webrtc_sig_server_addr);
  tmp_config_obj.set_sig_server_path(FLAGS_webrtc_sig_server_path);
  tmp_config_obj.set_sig_server_strict(FLAGS_verify_sig_server_certificate);
  tmp_config_obj.set_sig_server_headers_path(FLAGS_sig_server_headers_file);

  auto tcp_range  = ParsePortRange(FLAGS_tcp_port_range);
  tmp_config_obj.set_webrtc_tcp_port_range(tcp_range);
  auto udp_range  = ParsePortRange(FLAGS_udp_port_range);
  tmp_config_obj.set_webrtc_udp_port_range(udp_range);

  tmp_config_obj.set_webrtc_enable_adb_websocket(
          FLAGS_webrtc_enable_adb_websocket);

  tmp_config_obj.set_enable_gnss_grpc_proxy(FLAGS_start_gnss_proxy);

  tmp_config_obj.set_enable_vehicle_hal_grpc_server(
      FLAGS_enable_vehicle_hal_grpc_server);

  tmp_config_obj.set_enable_metrics(FLAGS_report_anonymous_usage_stats);

  if (!FLAGS_boot_slot.empty()) {
      tmp_config_obj.set_boot_slot(FLAGS_boot_slot);
  }

  tmp_config_obj.set_cuttlefish_env_path(GetCuttlefishEnvPath());

  tmp_config_obj.set_ril_dns(FLAGS_ril_dns);

  tmp_config_obj.set_vhost_net(FLAGS_vhost_net);

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

  tmp_config_obj.set_rootcanal_config_file(
      FLAGS_bluetooth_controller_properties_file);
  tmp_config_obj.set_rootcanal_default_commands_file(
      FLAGS_bluetooth_default_commands_file);

  tmp_config_obj.set_record_screen(FLAGS_record_screen);

  // netsim flags allow all radios or selecting a specific radio
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

  tmp_config_obj.set_protected_vm(FLAGS_protected_vm);

  // old flags but vectorized for multi-device instances
  std::vector<std::string> gnss_file_paths = android::base::Split(FLAGS_gnss_file_path, ",");
  std::vector<std::string> fixed_location_file_paths =
      android::base::Split(FLAGS_fixed_location_file_path, ",");
  std::vector<std::string> x_res_vec = android::base::Split(FLAGS_x_res, ",");
  std::vector<std::string> y_res_vec = android::base::Split(FLAGS_y_res, ",");
  std::vector<std::string> dpi_vec = android::base::Split(FLAGS_dpi, ",");
  std::vector<std::string> refresh_rate_hz_vec =
      android::base::Split(FLAGS_refresh_rate_hz, ",");
  std::vector<std::string> memory_mb_vec =
      android::base::Split(FLAGS_memory_mb, ",");
  std::vector<std::string> camera_server_port_vec =
      android::base::Split(FLAGS_camera_server_port, ",");
  std::vector<std::string> vsock_guest_cid_vec =
      android::base::Split(FLAGS_vsock_guest_cid, ",");
  std::vector<std::string> cpus_vec = android::base::Split(FLAGS_cpus, ",");
  std::vector<std::string> blank_data_image_mb_vec =
      android::base::Split(FLAGS_blank_data_image_mb, ",");
  std::vector<std::string> gdb_port_vec = android::base::Split(FLAGS_gdb_port, ",");
  std::vector<std::string> setupwizard_mode_vec =
      android::base::Split(FLAGS_setupwizard_mode, ",");
  std::vector<std::string> userdata_format_vec =
      android::base::Split(FLAGS_userdata_format, ",");
  std::vector<std::string> guest_enforce_security_vec =
      android::base::Split(FLAGS_guest_enforce_security, ",");
  std::vector<std::string> use_random_serial_vec =
      android::base::Split(FLAGS_use_random_serial, ",");
  std::vector<std::string> use_allocd_vec =
      android::base::Split(FLAGS_use_allocd, ",");
  std::vector<std::string> use_sdcard_vec =
      android::base::Split(FLAGS_use_sdcard, ",");
  std::vector<std::string> pause_in_bootloader_vec =
      android::base::Split(FLAGS_pause_in_bootloader, ",");
  std::vector<std::string> daemon_vec =
      android::base::Split(FLAGS_daemon, ",");
  std::vector<std::string> enable_minimal_mode_vec =
      android::base::Split(FLAGS_enable_minimal_mode, ",");
  std::vector<std::string> enable_modem_simulator_vec =
      android::base::Split(FLAGS_enable_modem_simulator, ",");
  std::vector<std::string> modem_simulator_count_vec =
      android::base::Split(FLAGS_modem_simulator_count, ",");
  std::vector<std::string> modem_simulator_sim_type_vec =
      android::base::Split(FLAGS_modem_simulator_sim_type, ",");

  // new instance specific flags (moved from common flags)
  std::vector<std::string> gem5_binary_dirs =
      android::base::Split(FLAGS_gem5_binary_dir, ",");
  std::vector<std::string> gem5_checkpoint_dirs =
      android::base::Split(FLAGS_gem5_checkpoint_dir, ",");
  std::vector<std::string> data_policies =
      android::base::Split(FLAGS_data_policy, ",");

  auto instance_nums = InstanceNumsCalculator().FromGlobalGflags().Calculate();
  if (!instance_nums.ok()) {
    LOG(ERROR) << instance_nums.error().Message();
    LOG(DEBUG) << instance_nums.error().Trace();
    abort();
  }

  CHECK(FLAGS_use_overlay || instance_nums->size() == 1)
      << "`--use_overlay=false` is incompatible with multiple instances";
  CHECK(instance_nums->size() > 0) << "Require at least one instance.";
  auto rootcanal_instance_num = *instance_nums->begin() - 1;
  if (FLAGS_rootcanal_instance_num > 0) {
    rootcanal_instance_num = FLAGS_rootcanal_instance_num - 1;
  }
  tmp_config_obj.set_rootcanal_hci_port(7300 + rootcanal_instance_num);
  tmp_config_obj.set_rootcanal_link_port(7400 + rootcanal_instance_num);
  tmp_config_obj.set_rootcanal_test_port(7500 + rootcanal_instance_num);
  tmp_config_obj.set_rootcanal_link_ble_port(7600 + rootcanal_instance_num);
  LOG(DEBUG) << "rootcanal_instance_num: " << rootcanal_instance_num;
  LOG(DEBUG) << "launch rootcanal: " << (FLAGS_rootcanal_instance_num <= 0);
  bool is_first_instance = true;
  int instance_index = 0;
  for (const auto& num : *instance_nums) {
    bool use_allocd;
    if (instance_index >= use_allocd_vec.size()) {
      use_allocd = CF_EXPECT(ParseBool(use_allocd_vec[0],
                                    "use_allocd"));
    } else {
      use_allocd = CF_EXPECT(ParseBool(
          use_allocd_vec[instance_index], "use_allocd"));
    }

    IfaceConfig iface_config;
    if (use_allocd) {
      auto iface_opt = AllocateNetworkInterfaces();
      if (!iface_opt.has_value()) {
        LOG(FATAL) << "Failed to acquire network interfaces";
      }
      iface_config = iface_opt.value();
    } else {
      iface_config = DefaultNetworkInterfaces(num);
    }

    bool use_random_serial;
    if (instance_index >= use_random_serial_vec.size()) {
      use_random_serial = CF_EXPECT(ParseBool(use_random_serial_vec[0],
                                    "use_random_serial"));
    } else {
      use_random_serial = CF_EXPECT(ParseBool(
          use_random_serial_vec[instance_index], "use_random_serial"));
    }
    auto instance = tmp_config_obj.ForInstance(num);
    auto const_instance =
        const_cast<const CuttlefishConfig&>(tmp_config_obj).ForInstance(num);
    instance.set_use_allocd(use_allocd);
    if (use_random_serial) {
      instance.set_serial_number(
          RandomSerialNumber("CFCVD" + std::to_string(num)));
    } else {
      instance.set_serial_number(FLAGS_serial_number + std::to_string(num));
    }

    int vsock_guest_cid_int;
    if (instance_index < vsock_guest_cid_vec.size()) {
      CF_EXPECT(
          android::base::ParseInt(vsock_guest_cid_vec[instance_index].c_str(),
                                  &vsock_guest_cid_int),
          "Failed to parse value \"" << vsock_guest_cid_vec[instance_index]
                                     << "\" for vsock_guest_cid");
    } else {
      CF_EXPECT(android::base::ParseInt(vsock_guest_cid_vec[0].c_str(),
                                        &vsock_guest_cid_int),
                "Failed to parse value \"" << vsock_guest_cid_vec[0]
                                           << "\" for vsock_guest_cid");
    }

    // call this before all stuff that has vsock server: e.g. touchpad, keyboard, etc
    const auto vsock_guest_cid = vsock_guest_cid_int + num - GetInstance();
    instance.set_vsock_guest_cid(vsock_guest_cid);
    auto calc_vsock_port = [vsock_guest_cid](const int base_port) {
      // a base (vsock) port is like 9600 for modem_simulator, etc
      return cuttlefish::GetVsockServerPort(base_port, vsock_guest_cid);
    };
    instance.set_session_id(iface_config.mobile_tap.session_id);

    int cpus_int;
    if (instance_index < cpus_vec.size()) {
      CF_EXPECT(
          android::base::ParseInt(cpus_vec[instance_index].c_str(), &cpus_int),
          "Failed to parse value \"" << cpus_vec[instance_index]
                                     << "\" for cpus");
    } else {
      CF_EXPECT(android::base::ParseInt(cpus_vec[0].c_str(), &cpus_int),
                "Failed to parse value \"" << cpus_vec[0] << "\" for cpus");
    }
    instance.set_cpus(cpus_int);
    // TODO(weihsu): before vectorizing smt flag,
    // make sure all instances have multiple of 2 then SMT mode
    // if any of instance doesn't have multiple of 2 then NOT SMT
    CF_EXPECT(!FLAGS_smt || cpus_int % 2 == 0,
              "CPUs must be a multiple of 2 in SMT mode");

    // new instance specific flags (moved from common flags)
    CF_EXPECT(instance_index < kernel_configs.size(),
              "instance_index " << instance_index << " out of boundary "
                                << kernel_configs.size());
    instance.set_target_arch(kernel_configs[instance_index].target_arch);
    instance.set_console(FLAGS_console);
    instance.set_kgdb(FLAGS_console && FLAGS_kgdb);

    int blank_data_image_mb_int;
    if (instance_index < blank_data_image_mb_vec.size()) {
      CF_EXPECT(android::base::ParseInt(
                    blank_data_image_mb_vec[instance_index].c_str(),
                    &blank_data_image_mb_int),
                "Failed to parse value \""
                    << blank_data_image_mb_vec[instance_index]
                    << "\" for blank_data_image_mb");
    } else {
      CF_EXPECT(android::base::ParseInt(blank_data_image_mb_vec[0].c_str(),
                                        &blank_data_image_mb_int),
                "Failed to parse value \"" << blank_data_image_mb_vec[0]
                                           << "\" for blank_data_image_mb");
    }
    instance.set_blank_data_image_mb(blank_data_image_mb_int);

    int gdb_port_int;
    if (instance_index < gdb_port_vec.size()) {
      CF_EXPECT(android::base::ParseInt(gdb_port_vec[instance_index].c_str(),
                                        &gdb_port_int),
                "Failed to parse value \"" << gdb_port_vec[instance_index]
                                           << "\" for gdb_port");
    } else {
      CF_EXPECT(
          android::base::ParseInt(gdb_port_vec[0].c_str(), &gdb_port_int),
          "Failed to parse value \"" << gdb_port_vec[0] << "\" for gdb_port");
    }
    instance.set_gdb_port(gdb_port_int);

    std::vector<CuttlefishConfig::DisplayConfig> display_configs;
    auto display0 = ParseDisplayConfig(FLAGS_display0);
    if (display0) {
      display_configs.push_back(*display0);
    }
    auto display1 = ParseDisplayConfig(FLAGS_display1);
    if (display1) {
      display_configs.push_back(*display1);
    }
    auto display2 = ParseDisplayConfig(FLAGS_display2);
    if (display2) {
      display_configs.push_back(*display2);
    }
    auto display3 = ParseDisplayConfig(FLAGS_display3);
    if (display3) {
      display_configs.push_back(*display3);
    }

    int x_res = 0;
    if (instance_index < x_res_vec.size()) {
      CF_EXPECT(
          android::base::ParseInt(x_res_vec[instance_index].c_str(), &x_res),
          "Failed to parse value \"" << x_res_vec[instance_index]
                                     << "\" for x_res");
    } else if (x_res_vec.size() == 1) {
      CF_EXPECT(android::base::ParseInt(x_res_vec[0].c_str(), &x_res),
                "Failed to parse value \"" << x_res_vec[0] << "\" for x_res");
    }
    int y_res = 0;
    if (instance_index < y_res_vec.size()) {
      CF_EXPECT(
          android::base::ParseInt(y_res_vec[instance_index].c_str(), &y_res),
          "Failed to parse value \"" << y_res_vec[instance_index]
                                     << "\" for y_res");
    } else if (y_res_vec.size() == 1) {
      CF_EXPECT(android::base::ParseInt(y_res_vec[0].c_str(), &y_res),
                "Failed to parse value \"" << y_res_vec[0] << "\" for y_res");
    }
    int dpi = 0;
    if (instance_index < dpi_vec.size()) {
      CF_EXPECT(android::base::ParseInt(dpi_vec[instance_index].c_str(), &dpi),
                "Failed to parse value \"" << dpi_vec[instance_index]
                                           << "\" for dpi");
    } else if (dpi_vec.size() == 1) {
      CF_EXPECT(android::base::ParseInt(dpi_vec[0].c_str(), &dpi),
                "Failed to parse value \"" << dpi_vec[0] << "\" for dpi");
    }
    int refresh_rate_hz = 0;
    if (instance_index < refresh_rate_hz_vec.size()) {
      CF_EXPECT(
          android::base::ParseInt(refresh_rate_hz_vec[instance_index].c_str(),
                                  &refresh_rate_hz),
          "Failed to parse value \"" << refresh_rate_hz_vec[instance_index]
                                     << "\" for refresh_rate_hz");
    } else if (refresh_rate_hz_vec.size() == 1) {
      CF_EXPECT(android::base::ParseInt(refresh_rate_hz_vec[0].c_str(),
                                        &refresh_rate_hz),
                "Failed to parse value \"" << refresh_rate_hz_vec[0]
                                           << "\" for refresh_rate_hz");
    }
    if (x_res > 0 && y_res > 0) {
      if (display_configs.empty()) {
        display_configs.push_back({
            .width = x_res,
            .height = y_res,
            .dpi = dpi,
            .refresh_rate_hz = refresh_rate_hz,
          });
      } else {
        LOG(WARNING) << "Ignoring --x_res and --y_res when --displayN specified.";
      }
    }
    instance.set_display_configs(display_configs);

    int memory_mb;
    if (instance_index >= memory_mb_vec.size()) {
      CF_EXPECT(
          android::base::ParseInt(memory_mb_vec[0].c_str(), &memory_mb),
          "Failed to parse value \"" << memory_mb_vec[0] << "\" for memory_mb");
    } else {
      CF_EXPECT(android::base::ParseInt(memory_mb_vec[instance_index].c_str(),
                                        &memory_mb),
                "Failed to parse value \"" << memory_mb_vec[instance_index]
                                           << "\" for memory_mb");
    }
    instance.set_memory_mb(memory_mb);
    instance.set_ddr_mem_mb(memory_mb * 2);

    if (instance_index >= setupwizard_mode_vec.size()) {
      CF_EXPECT(instance.set_setupwizard_mode(setupwizard_mode_vec[0]),
                "setting setupwizard flag failed");
    } else {
      CF_EXPECT(
          instance.set_setupwizard_mode(setupwizard_mode_vec[instance_index]),
          "setting setupwizard flag failed");
    }

    if (instance_index >= userdata_format_vec.size()) {
      instance.set_userdata_format(userdata_format_vec[0]);
    } else {
      instance.set_userdata_format(userdata_format_vec[instance_index]);
    }

    bool guest_enforce_security;
    if (instance_index >= guest_enforce_security_vec.size()) {
      guest_enforce_security = CF_EXPECT(
          ParseBool(guest_enforce_security_vec[0], "guest_enforce_security"));
    } else {
      guest_enforce_security = CF_EXPECT(ParseBool(
          guest_enforce_security_vec[instance_index], "guest_enforce_security"));
    }
    instance.set_guest_enforce_security(guest_enforce_security);

    bool pause_in_bootloader;
    if (instance_index >= pause_in_bootloader_vec.size()) {
      pause_in_bootloader = CF_EXPECT(ParseBool(pause_in_bootloader_vec[0],
                                    "pause_in_bootloader"));
    } else {
      pause_in_bootloader = CF_EXPECT(ParseBool(
          pause_in_bootloader_vec[instance_index], "pause_in_bootloader"));
    }
    instance.set_pause_in_bootloader(pause_in_bootloader);

    bool daemon;
    if (instance_index >= daemon_vec.size()) {
      daemon = CF_EXPECT(ParseBool(daemon_vec[0], "daemon"));
    } else {
      daemon = CF_EXPECT(ParseBool(daemon_vec[instance_index], "daemon"));
    }
    instance.set_run_as_daemon(daemon);

    bool enable_minimal_mode;
    if (instance_index >= enable_minimal_mode_vec.size()) {
      enable_minimal_mode = CF_EXPECT(
          ParseBool(enable_minimal_mode_vec[0], "enable_minimal_mode"));
    } else {
      enable_minimal_mode = CF_EXPECT(ParseBool(
          enable_minimal_mode_vec[instance_index], "enable_minimal_mode"));
    }
    bool enable_modem_simulator;
    if (instance_index >= enable_modem_simulator_vec.size()) {
      enable_modem_simulator = CF_EXPECT(
          ParseBool(enable_modem_simulator_vec[0], "enable_modem_simulator"));
    } else {
      enable_modem_simulator =
          CF_EXPECT(ParseBool(enable_modem_simulator_vec[instance_index],
                              "enable_modem_simulator"));
    }
    int modem_simulator_count;
    if (instance_index >= modem_simulator_count_vec.size()) {
      CF_EXPECT(android::base::ParseInt(modem_simulator_count_vec[0].c_str(),
                                        &modem_simulator_count),
                "Failed to parse value \"" << modem_simulator_count_vec[0]
                                           << "\" for modem_simulator_count");
    } else {
      CF_EXPECT(android::base::ParseInt(
                    modem_simulator_count_vec[instance_index].c_str(),
                    &modem_simulator_count),
                "Failed to parse value \""
                    << modem_simulator_count_vec[instance_index]
                    << "\" for modem_simulator_count");
    }
    int modem_simulator_sim_type;
    if (instance_index >= modem_simulator_sim_type_vec.size()) {
      CF_EXPECT(android::base::ParseInt(modem_simulator_sim_type_vec[0].c_str(),
                                        &modem_simulator_sim_type),
                "Failed to parse value \""
                    << modem_simulator_sim_type_vec[0]
                    << "\" for modem_simulator_sim_type");
    } else {
      CF_EXPECT(android::base::ParseInt(
                    modem_simulator_sim_type_vec[instance_index].c_str(),
                    &modem_simulator_sim_type),
                "Failed to parse value \""
                    << modem_simulator_sim_type_vec[instance_index]
                    << "\" for modem_simulator_sim_type");
    }
    instance.set_enable_modem_simulator(enable_modem_simulator &&
                                        !enable_minimal_mode);
    instance.set_modem_simulator_instance_number(modem_simulator_count);
    instance.set_modem_simulator_sim_type(modem_simulator_sim_type);

    instance.set_enable_minimal_mode(enable_minimal_mode);

    int camera_server_port;
    if (instance_index < camera_server_port_vec.size()) {
      CF_EXPECT(android::base::ParseInt(
                    camera_server_port_vec[instance_index].c_str(),
                    &camera_server_port),
                "Failed to parse value \""
                    << camera_server_port_vec[instance_index]
                    << "\" for camera_server_port");
    } else {
      CF_EXPECT(android::base::ParseInt(camera_server_port_vec[0].c_str(),
                                        &camera_server_port),
                "Failed to parse value \"" << camera_server_port_vec[0]
                                           << "\" for camera_server_port");
    }
    instance.set_camera_server_port(camera_server_port);

    if (instance_index < gem5_binary_dirs.size()) {
      instance.set_gem5_binary_dir(gem5_binary_dirs[instance_index]);
    } else if (gem5_binary_dirs.size() == 1) {
      // support legacy flag input in multi-device which set one and same flag to all instances
      instance.set_gem5_binary_dir(gem5_binary_dirs[0]);
    }
    if (instance_index < gem5_checkpoint_dirs.size()) {
      instance.set_gem5_checkpoint_dir(gem5_checkpoint_dirs[instance_index]);
    } else if (gem5_checkpoint_dirs.size() == 1) {
      // support legacy flag input in multi-device which set one and same flag to all instances
      instance.set_gem5_checkpoint_dir(gem5_checkpoint_dirs[0]);
    }
    if (instance_index < data_policies.size()) {
      instance.set_data_policy(data_policies[instance_index]);
    } else if (data_policies.size() == 1) {
      // support legacy flag input in multi-device which set one and same flag
      // to all instances
      instance.set_data_policy(data_policies[0]);
    }

    instance.set_mobile_bridge_name(StrForInstance("cvd-mbr-", num));
    instance.set_mobile_tap_name(iface_config.mobile_tap.name);
    instance.set_wifi_tap_name(iface_config.wireless_tap.name);
    instance.set_ethernet_tap_name(iface_config.ethernet_tap.name);

    instance.set_uuid(FLAGS_uuid);

    instance.set_modem_simulator_host_id(1000 + num);  // Must be 4 digits
    // the deprecated vnc was 6444 + num - 1, and qemu_vnc was vnc - 5900
    instance.set_qemu_vnc_server_port(544 + num - 1);
    instance.set_adb_host_port(6520 + num - 1);
    instance.set_adb_ip_and_port("0.0.0.0:" + std::to_string(6520 + num - 1));
    instance.set_tombstone_receiver_port(calc_vsock_port(6600));
    instance.set_vehicle_hal_server_port(9300 + num - 1);
    instance.set_audiocontrol_server_port(9410);  /* OK to use the same port number across instances */
    instance.set_config_server_port(calc_vsock_port(6800));

    if (tmp_config_obj.gpu_mode() != kGpuModeDrmVirgl &&
        tmp_config_obj.gpu_mode() != kGpuModeGfxStream) {
      if (vm_manager_vec[0] == QemuManager::name()) {
        instance.set_keyboard_server_port(calc_vsock_port(7000));
        instance.set_touch_server_port(calc_vsock_port(7100));
      }
    }

    instance.set_gnss_grpc_proxy_server_port(7200 + num -1);

    if (instance_index < gnss_file_paths.size()) {
      instance.set_gnss_file_path(gnss_file_paths[instance_index]);
    }
    if (instance_index < fixed_location_file_paths.size()) {
      instance.set_fixed_location_file_path(
          fixed_location_file_paths[instance_index]);
    }

    std::vector<std::string> virtual_disk_paths;

    bool os_overlay = true;
    os_overlay &= !FLAGS_protected_vm;
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
    persistent_disk &= !FLAGS_protected_vm;
    persistent_disk &= vm_manager_vec[0] != Gem5Manager::name();
    if (persistent_disk) {
      auto path = const_instance.PerInstancePath("persistent_composite.img");
      virtual_disk_paths.push_back(path);
    }

    bool use_sdcard;
    if (instance_index >= use_sdcard_vec.size()) {
      use_sdcard = CF_EXPECT(ParseBool(use_sdcard_vec[0],
                                    "use_sdcard"));
    } else {
      use_sdcard = CF_EXPECT(ParseBool(
          use_sdcard_vec[instance_index], "use_sdcard"));
    }
    instance.set_use_sdcard(use_sdcard);

    bool sdcard = true;
    sdcard &= use_sdcard;
    sdcard &= !FLAGS_protected_vm;
    if (sdcard) {
      virtual_disk_paths.push_back(const_instance.sdcard_path());
    }

    instance.set_virtual_disk_paths(virtual_disk_paths);

    // We'd like to set mac prefix to be 5554, 5555, 5556, ... in normal cases.
    // When --base_instance_num=3, this might be 5556, 5557, 5558, ... (skipping
    // first two)
    instance.set_wifi_mac_prefix(5554 + (num - 1));

    instance.set_start_webrtc_signaling_server(false);

    if (FLAGS_webrtc_device_id.empty()) {
      // Use the instance's name as a default
      instance.set_webrtc_device_id(const_instance.instance_name());
    } else {
      std::string device_id = FLAGS_webrtc_device_id;
      size_t pos;
      while ((pos = device_id.find("{num}")) != std::string::npos) {
        device_id.replace(pos, strlen("{num}"), std::to_string(num));
      }
      instance.set_webrtc_device_id(device_id);
    }
    if (!is_first_instance || !FLAGS_start_webrtc) {
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
    const bool start_wmediumd =
        FLAGS_vhost_user_mac80211_hwsim.empty() && is_first_instance;
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

    instance.set_start_ap(!FLAGS_ap_rootfs_image.empty() &&
                          !FLAGS_ap_kernel_image.empty() && start_wmediumd);

    is_first_instance = false;

    // instance.modem_simulator_ports := "" or "[port,]*port"
    if (modem_simulator_count > 0) {
      std::stringstream modem_ports;
      for (auto index {0}; index < modem_simulator_count - 1; index++) {
        auto port = 9600 + (modem_simulator_count * (num - 1)) + index;
        modem_ports << calc_vsock_port(port) << ",";
      }
      auto port = 9600 + (modem_simulator_count * (num - 1)) +
                  modem_simulator_count - 1;
      modem_ports << calc_vsock_port(port);
      instance.set_modem_simulator_ports(modem_ports.str());
    } else {
      instance.set_modem_simulator_ports("");
    }
    instance_index++;
  }  // end of num_instances loop

  tmp_config_obj.set_smt(FLAGS_smt);

  std::vector<std::string> names;
  for (const auto& instance : tmp_config_obj.Instances()) {
    names.emplace_back(instance.instance_name());
  }
  tmp_config_obj.set_instance_names(names);

  tmp_config_obj.set_enable_sandbox(FLAGS_enable_sandbox);

  tmp_config_obj.set_enable_audio(FLAGS_enable_audio);

  DiskImageFlagsVectorization(tmp_config_obj, fetcher_config);

  return tmp_config_obj;
}

void SetDefaultFlagsForQemu(Arch target_arch) {
  // for now, we don't set non-default options for QEMU
  if (FLAGS_gpu_mode == kGpuModeGuestSwiftshader && !FLAGS_start_webrtc) {
    // This makes WebRTC the default streamer unless the user requests
    // another via a --star_<streamer> flag, while at the same time it's
    // possible to run without any streamer by setting --start_webrtc=false.
    SetCommandLineOptionWithMode("start_webrtc", "true", SET_FLAGS_DEFAULT);
  }
  std::string default_bootloader =
      DefaultHostArtifactsPath("etc/bootloader_");
  if(target_arch == Arch::Arm) {
      // Bootloader is unstable >512MB RAM on 32-bit ARM
      SetCommandLineOptionWithMode("memory_mb", "512", SET_FLAGS_VALUE);
      default_bootloader += "arm";
  } else if (target_arch == Arch::Arm64) {
      default_bootloader += "aarch64";
  } else {
      default_bootloader += "x86_64";
  }
  default_bootloader += "/bootloader.qemu";
  SetCommandLineOptionWithMode("bootloader", default_bootloader.c_str(),
                               SET_FLAGS_DEFAULT);
}

void SetDefaultFlagsForCrosvm() {
  if (!FLAGS_start_webrtc) {
    // This makes WebRTC the default streamer unless the user requests
    // another via a --star_<streamer> flag, while at the same time it's
    // possible to run without any streamer by setting --start_webrtc=false.
    SetCommandLineOptionWithMode("start_webrtc", "true", SET_FLAGS_DEFAULT);
  }

  std::set<Arch> supported_archs{Arch::X86_64};
  bool default_enable_sandbox =
      supported_archs.find(HostArch()) != supported_archs.end() &&
      EnsureDirectoryExists(kCrosvmVarEmptyDir).ok() &&
      IsDirectoryEmpty(kCrosvmVarEmptyDir) && !IsRunningInContainer();
  SetCommandLineOptionWithMode("enable_sandbox",
                               (default_enable_sandbox ? "true" : "false"),
                               SET_FLAGS_DEFAULT);

  std::vector<std::string> system_image_dir =
      android::base::Split(FLAGS_system_image_dir, ",");
  std::string cur_system_image_dir = "";
  std::string default_bootloader = "";
  auto instance_nums =
      InstanceNumsCalculator().FromGlobalGflags().Calculate();
  for (int instance_index = 0; instance_index < instance_nums->size(); instance_index++) {
    if (instance_index >= system_image_dir.size()) {
      cur_system_image_dir = system_image_dir[0];
    } else {
      cur_system_image_dir = system_image_dir[instance_index];
    }
    cur_system_image_dir += "/bootloader";
    if (instance_index > 0) {
      default_bootloader += ",";
    }
    default_bootloader += cur_system_image_dir;
  }
  SetCommandLineOptionWithMode("bootloader", default_bootloader.c_str(),
                               SET_FLAGS_DEFAULT);
}

void SetDefaultFlagsForGem5() {
  // TODO: Add support for gem5 gpu models
  SetCommandLineOptionWithMode("gpu_mode", kGpuModeGuestSwiftshader,
                               SET_FLAGS_DEFAULT);

  SetCommandLineOptionWithMode("cpus", "1", SET_FLAGS_DEFAULT);
}

Result<std::vector<KernelConfig>> GetKernelConfigAndSetDefaults() {
  CF_EXPECT(ResolveInstanceFiles(), "Failed to resolve instance files");

  std::vector<KernelConfig> kernel_configs = CF_EXPECT(ReadKernelConfig());

  // TODO(weihsu), b/250988697:
  // assume all instances are using same VM manager/app/arch,
  // later that multiple instances may use different VM manager/app/arch

  // Temporary add this checking to make sure all instances have same target_arch
  // and bootconfig_supported. This checking should be removed later.
  for (int instance_index = 1; instance_index < kernel_configs.size(); instance_index++) {
    CF_EXPECT(kernel_configs[0].target_arch == kernel_configs[instance_index].target_arch,
              "all instance target_arch should be same");
    CF_EXPECT(kernel_configs[0].bootconfig_supported ==
              kernel_configs[instance_index].bootconfig_supported,
              "all instance bootconfig_supported should be same");
  }
  if (FLAGS_vm_manager == "") {
    if (IsHostCompatible(kernel_configs[0].target_arch)) {
      FLAGS_vm_manager = CrosvmManager::name();
    } else {
      FLAGS_vm_manager = QemuManager::name();
    }
  }
  // TODO(weihsu), b/250988697:
  // Currently, all instances should use same vmm
  std::vector<std::string> vm_manager_vec =
      android::base::Split(FLAGS_vm_manager, ",");

  if (vm_manager_vec[0] == QemuManager::name()) {
    SetDefaultFlagsForQemu(kernel_configs[0].target_arch);
  } else if (vm_manager_vec[0] == CrosvmManager::name()) {
    SetDefaultFlagsForCrosvm();
  } else if (vm_manager_vec[0] == Gem5Manager::name()) {
    // TODO: Get the other architectures working
    if (kernel_configs[0].target_arch != Arch::Arm64) {
      return CF_ERR("Gem5 only supports ARM64");
    }
    SetDefaultFlagsForGem5();
  } else {
    return CF_ERR("Unknown Virtual Machine Manager: " << FLAGS_vm_manager);
  }
  if (vm_manager_vec[0] != Gem5Manager::name()) {
    auto host_operator_present =
        cuttlefish::FileIsSocket(HOST_OPERATOR_SOCKET_PATH);
    // The default for starting signaling server depends on whether or not webrtc
    // is to be started and the presence of the host orchestrator.
    SetCommandLineOptionWithMode(
        "start_webrtc_sig_server",
        FLAGS_start_webrtc && !host_operator_present ? "true" : "false",
        SET_FLAGS_DEFAULT);
    SetCommandLineOptionWithMode(
        "webrtc_sig_server_addr",
        host_operator_present ? HOST_OPERATOR_SOCKET_PATH : "0.0.0.0",
        SET_FLAGS_DEFAULT);
  }
  // Set the env variable to empty (in case the caller passed a value for it).
  unsetenv(kCuttlefishConfigEnvVarName);

  return kernel_configs;
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
