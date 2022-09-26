#include "host/commands/assemble_cvd/flags.h"

#include <android-base/logging.h>
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
#include "host/commands/assemble_cvd/alloc.h"
#include "host/commands/assemble_cvd/boot_config.h"
#include "host/commands/assemble_cvd/disk_flags.h"
#include "host/libs/config/config_flag.h"
#include "host/libs/config/host_tools_version.h"
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

DEFINE_int32(cpus, 2, "Virtual CPU count.");
DEFINE_string(data_policy, "use_existing", "How to handle userdata partition."
            " Either 'use_existing', 'create_if_missing', 'resize_up_to', or "
            "'always_create'.");
DEFINE_int32(blank_data_image_mb, 0,
             "The size of the blank data image to generate, MB.");
DEFINE_int32(gdb_port, 0,
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
DEFINE_string(display0, "", kDisplayHelp);
DEFINE_string(display1, "", kDisplayHelp);
DEFINE_string(display2, "", kDisplayHelp);
DEFINE_string(display3, "", kDisplayHelp);

// TODO(b/171305898): mark these as deprecated after multi-display is fully
// enabled.
DEFINE_int32(x_res, 0, "Width of the screen in pixels");
DEFINE_int32(y_res, 0, "Height of the screen in pixels");
DEFINE_int32(dpi, 0, "Pixels per inch for the screen");
DEFINE_int32(refresh_rate_hz, 60, "Screen refresh rate in Hertz");
DEFINE_string(kernel_path, "",
              "Path to the kernel. Overrides the one from the boot image");
DEFINE_string(initramfs_path, "", "Path to the initramfs");
DEFINE_string(extra_kernel_cmdline, "",
              "Additional flags to put on the kernel command line");
DEFINE_string(extra_bootconfig_args, "",
              "Space-separated list of extra bootconfig args. "
              "Note: overwriting an existing bootconfig argument "
              "requires ':=' instead of '='.");
DEFINE_bool(guest_enforce_security, true,
            "Whether to run in enforcing mode (non permissive).");
DEFINE_int32(memory_mb, 0, "Total amount of memory available for guest, MB.");
DEFINE_string(serial_number, cuttlefish::ForCurrentInstance("CUTTLEFISHCVD"),
              "Serial number to use for the device");
DEFINE_bool(use_random_serial, false,
            "Whether to use random serial for the device.");
DEFINE_string(vm_manager, "",
              "What virtual machine manager to use, one of {qemu_cli, crosvm}");
DEFINE_string(gpu_mode, cuttlefish::kGpuModeAuto,
              "What gpu configuration to use, one of {auto, drm_virgl, "
              "gfxstream, guest_swiftshader}");
DEFINE_string(hwcomposer, cuttlefish::kHwComposerAuto,
              "What hardware composer to use, one of {auto, drm, ranchu} ");
DEFINE_string(gpu_capture_binary, "",
              "Path to the GPU capture binary to use when capturing GPU traces"
              "(ngfx, renderdoc, etc)");
DEFINE_bool(enable_gpu_udmabuf,
            false,
            "Use the udmabuf driver for zero-copy virtio-gpu");

DEFINE_bool(enable_gpu_angle,
            false,
            "Use ANGLE to provide GLES implementation (always true for"
            " guest_swiftshader");
DEFINE_bool(deprecated_boot_completed, false, "Log boot completed message to"
            " host kernel. This is only used during transition of our clients."
            " Will be deprecated soon.");

DEFINE_bool(use_allocd, false,
            "Acquire static resources from the resource allocator daemon.");
DEFINE_bool(enable_minimal_mode, false,
            "Only enable the minimum features to boot a cuttlefish device and "
            "support minimal UI interactions.\nNote: Currently only supports "
            "handheld/phone targets");
DEFINE_bool(pause_in_bootloader, false,
            "Stop the bootflow in u-boot. You can continue the boot by connecting "
            "to the device console and typing in \"boot\".");
DEFINE_bool(enable_host_bluetooth, true,
            "Enable the root-canal which is Bluetooth emulator in the host.");

DEFINE_string(bluetooth_controller_properties_file,
              "etc/rootcanal/data/controller_properties.json",
              "The configuartion file path for root-canal which is a Bluetooth "
              "emulator.");
DEFINE_string(
    bluetooth_default_commands_file, "etc/rootcanal/data/default_commands",
    "The default commands which root-canal executes when it launches.");

/**
 *
 * crosvm sandbox feature requires /var/empty and seccomp directory
 *
 * --enable-sandbox: will enforce the sandbox feature
 *                   failing to meet the requirements result in assembly_cvd termination
 *
 * --enable-sandbox=no, etc: will disable sandbox
 *
 * no option given: it is enabled if /var/empty exists and an empty directory
 *                             or if it does not exist and can be created
 *
 * if seccomp dir doesn't exist, assembly_cvd will terminate
 *
 * See SetDefaultFlagsForCrosvm()
 *
 */
DEFINE_bool(enable_sandbox,
            false,
            "Enable crosvm sandbox. Use this when you are sure about what you are doing.");

static const std::string kSeccompDir =
    std::string("usr/share/crosvm/") + cuttlefish::HostArchStr() + "-linux-gnu/seccomp";
DEFINE_string(seccomp_policy_dir, DefaultHostArtifactsPath(kSeccompDir),
              "With sandbox'ed crosvm, overrieds the security comp policy directory");

DEFINE_bool(start_webrtc, false, "Whether to start the webrtc process.");

DEFINE_string(
        webrtc_assets_dir, DefaultHostArtifactsPath("usr/share/webrtc/assets"),
        "[Experimental] Path to WebRTC webpage assets.");

DEFINE_string(
        webrtc_certs_dir, DefaultHostArtifactsPath("usr/share/webrtc/certs"),
        "[Experimental] Path to WebRTC certificates directory.");

DEFINE_string(
        webrtc_public_ip,
        "0.0.0.0",
        "[Deprecated] Ignored, webrtc can figure out its IP address");

DEFINE_bool(
        webrtc_enable_adb_websocket,
        false,
        "[Experimental] If enabled, exposes local adb service through a websocket.");

static constexpr auto HOST_OPERATOR_SOCKET_PATH = "/run/cuttlefish/operator";

DEFINE_bool(
    // The actual default for this flag is set with SetCommandLineOption() in
    // GetKernelConfigsAndSetDefaults() at the end of this file.
    start_webrtc_sig_server, true,
    "Whether to start the webrtc signaling server. This option only applies to "
    "the first instance, if multiple instances are launched they'll share the "
    "same signaling server, which is owned by the first one.");

DEFINE_string(webrtc_sig_server_addr, "",
              "The address of the webrtc signaling server.");

DEFINE_int32(
    webrtc_sig_server_port, 443,
    "The port of the signaling server if started outside of this launch. If "
    "-start_webrtc_sig_server is given it will choose 8443+instance_num1-1 and "
    "this parameter is ignored.");

// TODO (jemoreira): We need a much bigger range to reliably support several
// simultaneous connections.
DEFINE_string(tcp_port_range, "15550:15558",
              "The minimum and maximum TCP port numbers to allocate for ICE "
              "candidates as 'min:max'. To use any port just specify '0:0'");

DEFINE_string(udp_port_range, "15550:15558",
              "The minimum and maximum UDP port numbers to allocate for ICE "
              "candidates as 'min:max'. To use any port just specify '0:0'");

DEFINE_string(webrtc_sig_server_path, "/register_device",
              "The path section of the URL where the device should be "
              "registered with the signaling server.");

DEFINE_bool(webrtc_sig_server_secure, true,
            "Whether the WebRTC signaling server uses secure protocols (WSS vs WS).");

DEFINE_bool(verify_sig_server_certificate, false,
            "Whether to verify the signaling server's certificate with a "
            "trusted signing authority (Disallow self signed certificates). "
            "This is ignored if an insecure server is configured.");

DEFINE_string(sig_server_headers_file, "",
              "Path to a file containing HTTP headers to be included in the "
              "connection to the signaling server. Each header should be on a "
              "line by itself in the form <name>: <value>");

DEFINE_string(
    webrtc_device_id, "cvd-{num}",
    "The for the device to register with the signaling server. Every "
    "appearance of the substring '{num}' in the device id will be substituted "
    "with the instance number to support multiple instances");

DEFINE_string(uuid, cuttlefish::ForCurrentInstance(cuttlefish::kDefaultUuidPrefix),
              "UUID to use for the device. Random if not specified");
DEFINE_bool(daemon, false,
            "Run cuttlefish in background, the launcher exits on boot "
            "completed/failed");

DEFINE_string(setupwizard_mode, "DISABLED",
            "One of DISABLED,OPTIONAL,REQUIRED");

DEFINE_string(qemu_binary_dir, "/usr/bin",
              "Path to the directory containing the qemu binary to use");
DEFINE_string(crosvm_binary, HostBinaryPath("crosvm"),
              "The Crosvm binary to use");
DEFINE_string(gem5_binary_dir, HostBinaryPath("gem5"),
              "Path to the gem5 build tree root");
DEFINE_bool(restart_subprocesses, true, "Restart any crashed host process");
DEFINE_bool(enable_vehicle_hal_grpc_server, true, "Enables the vehicle HAL "
            "emulation gRPC server on the host");
DEFINE_string(bootloader, "", "Bootloader binary path");
DEFINE_string(boot_slot, "", "Force booting into the given slot. If empty, "
             "the slot will be chosen based on the misc partition if using a "
             "bootloader. It will default to 'a' if empty and not using a "
             "bootloader.");
DEFINE_int32(num_instances, 1, "Number of Android guests to launch");
DEFINE_string(report_anonymous_usage_stats, "", "Report anonymous usage "
            "statistics for metrics collection and analysis.");
DEFINE_string(ril_dns, "8.8.8.8", "DNS address of mobile network (RIL)");
DEFINE_bool(kgdb, false, "Configure the virtual device for debugging the kernel "
                         "with kgdb/kdb. The kernel must have been built with "
                         "kgdb support, and serial console must be enabled.");

DEFINE_bool(start_gnss_proxy, false, "Whether to start the gnss proxy.");

DEFINE_string(gnss_file_path, "",
              "Local gnss file path for the gnss proxy");

// by default, this modem-simulator is disabled
DEFINE_bool(enable_modem_simulator, true,
            "Enable the modem simulator to process RILD AT commands");
// modem_simulator_sim_type=2 for test CtsCarrierApiTestCases
DEFINE_int32(modem_simulator_sim_type, 1,
             "Sim type: 1 for normal, 2 for CtsCarrierApiTestCases");

DEFINE_bool(console, false, "Enable the serial console");

DEFINE_bool(vhost_net, false, "Enable vhost acceleration of networking");

DEFINE_string(
    vhost_user_mac80211_hwsim, "",
    "Unix socket path for vhost-user of mac80211_hwsim, typically served by "
    "wmediumd. You can set this when using an external wmediumd instance.");
DEFINE_string(wmediumd_config, "",
              "Path to the wmediumd config file. When missing, the default "
              "configuration is used which adds MAC addresses for up to 16 "
              "cuttlefish instances including AP.");
DEFINE_string(ap_rootfs_image,
              DefaultHostArtifactsPath("etc/openwrt/images/openwrt_rootfs"),
              "rootfs image for AP instance");
DEFINE_string(ap_kernel_image,
              DefaultHostArtifactsPath("etc/openwrt/images/kernel_for_openwrt"),
              "kernel image for AP instance");

DEFINE_bool(record_screen, false, "Enable screen recording. "
                                  "Requires --start_webrtc");

DEFINE_bool(smt, false, "Enable simultaneous multithreading (SMT/HT)");

DEFINE_int32(vsock_guest_cid,
             cuttlefish::GetDefaultVsockCid(),
             "vsock_guest_cid is used to determine the guest vsock cid as well as all the ports"
             "of all vsock servers such as tombstone or modem simulator(s)."
             "The vsock ports and guest vsock cid are a function of vsock_guest_cid and instance number."
             "An instance number of i th instance is determined by --num_instances=N and --base_instance_num=B"
             "The instance number of i th instance is B + i where i in [0, N-1] and B >= 1."
             "See --num_instances, and --base_instance_num for more information"
             "If --vsock_guest_cid=C is given and C >= 3, the guest vsock cid is C + i. Otherwise,"
             "the guest vsock cid is 2 + instance number, which is 2 + (B + i)."
             "If --vsock_guest_cid is not given, each vsock server port number for i th instance is"
             "base + instance number - 1. vsock_guest_cid is by default B + i + 2."
             "Thus, by default, each port is base + vsock_guest_cid - 3."
             "The same formula holds when --vsock_guest_cid=C is given, for algorithm's sake."
             "Each vsock server port number is base + C - 3.");

DEFINE_string(secure_hals, "keymint,gatekeeper",
              "Which HALs to use enable host security features for. Supports "
              "keymint and gatekeeper at the moment.");

DEFINE_bool(use_sdcard, true, "Create blank SD-Card image and expose to guest");

DEFINE_bool(protected_vm, false, "Boot in Protected VM mode");

DEFINE_bool(enable_audio, cuttlefish::HostArch() != cuttlefish::Arch::Arm64,
            "Whether to play or capture audio");

DEFINE_uint32(camera_server_port, 0, "camera vsock port");

DEFINE_string(userdata_format, "f2fs", "The userdata filesystem format");

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
Result<KernelConfig> ReadKernelConfig() {
  // QEMU isn't on Android, so always follow host arch
  KernelConfig ret{};
  ret.target_arch = HostArch();
  ret.bootconfig_supported = true;
  return ret;
}
#else
Result<KernelConfig> ReadKernelConfig() {
  // extract-ikconfig can be called directly on the boot image since it looks
  // for the ikconfig header in the image before extracting the config list.
  // This code is liable to break if the boot image ever includes the
  // ikconfig header outside the kernel.
  const std::string kernel_image_path =
      FLAGS_kernel_path.size() ? FLAGS_kernel_path : FLAGS_boot_image;

  Command ikconfig_cmd(HostBinaryPath("extract-ikconfig"));
  ikconfig_cmd.AddParameter(kernel_image_path);

  std::string current_path = StringFromEnv("PATH", "");
  std::string bin_folder = DefaultHostArtifactsPath("bin");
  ikconfig_cmd.SetEnvironment({"PATH=" + current_path + ":" + bin_folder});

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

  unlink(ikconfig_path.c_str());
  return kernel_config;
}
#endif  // #ifdef __ANDROID__

} // namespace

CuttlefishConfig InitializeCuttlefishConfiguration(
    const std::string& root_dir, int modem_simulator_count,
    KernelConfig kernel_config, fruit::Injector<>& injector) {
  CuttlefishConfig tmp_config_obj;

  for (const auto& fragment : injector.getMultibindings<ConfigFragment>()) {
    CHECK(tmp_config_obj.SaveFragment(*fragment))
        << "Failed to save fragment " << fragment->Name();
  }

  tmp_config_obj.set_root_dir(root_dir);

  tmp_config_obj.set_target_arch(kernel_config.target_arch);
  tmp_config_obj.set_bootconfig_supported(kernel_config.bootconfig_supported);
  auto vmm = GetVmManager(FLAGS_vm_manager, kernel_config.target_arch);
  if (!vmm) {
    LOG(FATAL) << "Invalid vm_manager: " << FLAGS_vm_manager;
  }
  tmp_config_obj.set_vm_manager(FLAGS_vm_manager);

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

  if (FLAGS_x_res > 0 && FLAGS_y_res > 0) {
    if (display_configs.empty()) {
      display_configs.push_back({
          .width = FLAGS_x_res,
          .height = FLAGS_y_res,
          .dpi = FLAGS_dpi,
          .refresh_rate_hz = FLAGS_refresh_rate_hz,
      });
    } else {
      LOG(WARNING) << "Ignoring --x_res and --y_res when --displayN specified.";
    }
  }

  tmp_config_obj.set_display_configs(display_configs);

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
      if (FLAGS_vm_manager == QemuManager::name()) {
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

  // The device needs to avoid having both hwcomposer2.4 and hwcomposer3
  // services running at the same time so warn the user to manually build
  // in drm_hwcomposer when needed.
  if (tmp_config_obj.hwcomposer() == kHwComposerAuto) {
    LOG(WARNING) << "In order to run with --hwcomposer=drm. Please make sure "
                    "Cuttlefish was built with "
                    "TARGET_ENABLE_DRMHWCOMPOSER=true.";
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
               ") does not work with vm_manager=" << FLAGS_vm_manager;
  }

  CHECK(!FLAGS_smt || FLAGS_cpus % 2 == 0)
      << "CPUs must be a multiple of 2 in SMT mode";
  tmp_config_obj.set_cpus(FLAGS_cpus);
  tmp_config_obj.set_smt(FLAGS_smt);

  tmp_config_obj.set_memory_mb(FLAGS_memory_mb);

  tmp_config_obj.set_setupwizard_mode(FLAGS_setupwizard_mode);

  auto secure_hals = android::base::Split(FLAGS_secure_hals, ",");
  tmp_config_obj.set_secure_hals(
      std::set<std::string>(secure_hals.begin(), secure_hals.end()));

  tmp_config_obj.set_gdb_port(FLAGS_gdb_port);

  tmp_config_obj.set_guest_enforce_security(FLAGS_guest_enforce_security);
  tmp_config_obj.set_extra_kernel_cmdline(FLAGS_extra_kernel_cmdline);
  tmp_config_obj.set_extra_bootconfig_args(FLAGS_extra_bootconfig_args);

  if (FLAGS_console) {
    SetCommandLineOptionWithMode("enable_sandbox", "false", SET_FLAGS_DEFAULT);
  }

  tmp_config_obj.set_console(FLAGS_console);
  tmp_config_obj.set_kgdb(FLAGS_console && FLAGS_kgdb);

  tmp_config_obj.set_host_tools_version(HostToolsCrc());

  tmp_config_obj.set_deprecated_boot_completed(FLAGS_deprecated_boot_completed);

  tmp_config_obj.set_qemu_binary_dir(FLAGS_qemu_binary_dir);
  tmp_config_obj.set_crosvm_binary(FLAGS_crosvm_binary);
  tmp_config_obj.set_gem5_binary_dir(FLAGS_gem5_binary_dir);

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

  tmp_config_obj.set_enable_modem_simulator(FLAGS_enable_modem_simulator &&
                                            !FLAGS_enable_minimal_mode);
  tmp_config_obj.set_modem_simulator_instance_number(modem_simulator_count);
  tmp_config_obj.set_modem_simulator_sim_type(FLAGS_modem_simulator_sim_type);

  tmp_config_obj.set_webrtc_enable_adb_websocket(
          FLAGS_webrtc_enable_adb_websocket);

  tmp_config_obj.set_run_as_daemon(FLAGS_daemon);

  tmp_config_obj.set_data_policy(FLAGS_data_policy);
  tmp_config_obj.set_blank_data_image_mb(FLAGS_blank_data_image_mb);

  tmp_config_obj.set_enable_gnss_grpc_proxy(FLAGS_start_gnss_proxy);

  tmp_config_obj.set_enable_vehicle_hal_grpc_server(
      FLAGS_enable_vehicle_hal_grpc_server);

  tmp_config_obj.set_bootloader(FLAGS_bootloader);

  tmp_config_obj.set_enable_metrics(FLAGS_report_anonymous_usage_stats);

  if (!FLAGS_boot_slot.empty()) {
      tmp_config_obj.set_boot_slot(FLAGS_boot_slot);
  }

  tmp_config_obj.set_cuttlefish_env_path(GetCuttlefishEnvPath());

  tmp_config_obj.set_ril_dns(FLAGS_ril_dns);

  tmp_config_obj.set_enable_minimal_mode(FLAGS_enable_minimal_mode);

  tmp_config_obj.set_vhost_net(FLAGS_vhost_net);

  tmp_config_obj.set_vhost_user_mac80211_hwsim(FLAGS_vhost_user_mac80211_hwsim);

  if ((FLAGS_ap_rootfs_image.empty()) != (FLAGS_ap_kernel_image.empty())) {
    LOG(FATAL) << "Either both ap_rootfs_image and ap_kernel_image should be "
                  "set or neither should be set.";
  }

  tmp_config_obj.set_ap_rootfs_image(FLAGS_ap_rootfs_image);
  tmp_config_obj.set_ap_kernel_image(FLAGS_ap_kernel_image);

  tmp_config_obj.set_wmediumd_config(FLAGS_wmediumd_config);

  tmp_config_obj.set_rootcanal_hci_port(7300);
  tmp_config_obj.set_rootcanal_link_port(7400);
  tmp_config_obj.set_rootcanal_test_port(7500);
  tmp_config_obj.set_rootcanal_config_file(
      FLAGS_bluetooth_controller_properties_file);
  tmp_config_obj.set_rootcanal_default_commands_file(
      FLAGS_bluetooth_default_commands_file);

  tmp_config_obj.set_record_screen(FLAGS_record_screen);

  tmp_config_obj.set_enable_host_bluetooth(FLAGS_enable_host_bluetooth);

  tmp_config_obj.set_protected_vm(FLAGS_protected_vm);

  tmp_config_obj.set_userdata_format(FLAGS_userdata_format);

  std::vector<int> num_instances;
  for (int i = 0; i < FLAGS_num_instances; i++) {
    num_instances.push_back(GetInstance() + i);
  }
  std::vector<std::string> gnss_file_paths = android::base::Split(FLAGS_gnss_file_path, ",");

  bool is_first_instance = true;
  for (const auto& num : num_instances) {
    IfaceConfig iface_config;
    if (FLAGS_use_allocd) {
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
    instance.set_use_allocd(FLAGS_use_allocd);
    if (FLAGS_use_random_serial) {
      instance.set_serial_number(
          RandomSerialNumber("CFCVD" + std::to_string(num)));
    } else {
      instance.set_serial_number(FLAGS_serial_number + std::to_string(num));
    }
    // call this before all stuff that has vsock server: e.g. touchpad, keyboard, etc
    const auto vsock_guest_cid = FLAGS_vsock_guest_cid + num - GetInstance();
    instance.set_vsock_guest_cid(vsock_guest_cid);
    auto calc_vsock_port = [vsock_guest_cid](const int base_port) {
      // a base (vsock) port is like 9600 for modem_simulator, etc
      return cuttlefish::GetVsockServerPort(base_port, vsock_guest_cid);
    };
    instance.set_session_id(iface_config.mobile_tap.session_id);

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
    instance.set_confui_host_vsock_port(7700 + num - 1);
    instance.set_tombstone_receiver_port(calc_vsock_port(6600));
    instance.set_vehicle_hal_server_port(9300 + num - 1);
    instance.set_audiocontrol_server_port(9410);  /* OK to use the same port number across instances */
    instance.set_config_server_port(calc_vsock_port(6800));

    if (tmp_config_obj.gpu_mode() != kGpuModeDrmVirgl &&
        tmp_config_obj.gpu_mode() != kGpuModeGfxStream) {
      if (FLAGS_vm_manager == QemuManager::name()) {
        instance.set_keyboard_server_port(calc_vsock_port(7000));
        instance.set_touch_server_port(calc_vsock_port(7100));
      }
    }

    instance.set_gnss_grpc_proxy_server_port(7200 + num -1);

    if (num <= gnss_file_paths.size()) {
      instance.set_gnss_file_path(gnss_file_paths[num-1]);
    }

    instance.set_camera_server_port(FLAGS_camera_server_port);

    if (FLAGS_protected_vm) {
      instance.set_virtual_disk_paths(
          {const_instance.PerInstancePath("os_composite.img")});
    } else {
      std::vector<std::string> virtual_disk_paths = {
          const_instance.PerInstancePath("persistent_composite.img"),
      };
      if (FLAGS_vm_manager != Gem5Manager::name()) {
        virtual_disk_paths.insert(virtual_disk_paths.begin(),
            const_instance.PerInstancePath("overlay.img"));
      } else {
        // Gem5 already uses CoW wrappers around disk images
        virtual_disk_paths.insert(virtual_disk_paths.begin(),
            tmp_config_obj.os_composite_disk_path());
      }
      if (FLAGS_use_sdcard) {
        virtual_disk_paths.push_back(const_instance.sdcard_path());
      }
      instance.set_virtual_disk_paths(virtual_disk_paths);
    }

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

    instance.set_start_rootcanal(is_first_instance);

    instance.set_start_ap(!FLAGS_ap_rootfs_image.empty() &&
                          !FLAGS_ap_kernel_image.empty() && is_first_instance);

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
  } // end of num_instances loop

  std::vector<std::string> names;
  for (const auto& instance : tmp_config_obj.Instances()) {
    names.emplace_back(instance.instance_name());
  }
  tmp_config_obj.set_instance_names(names);

  tmp_config_obj.set_enable_sandbox(FLAGS_enable_sandbox);

  // Audio is not available for Arm64
  SetCommandLineOptionWithMode(
      "enable_audio",
      (cuttlefish::HostArch() == cuttlefish::Arch::Arm64) ? "false" : "true",
      SET_FLAGS_DEFAULT);
  tmp_config_obj.set_enable_audio(FLAGS_enable_audio);

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

  std::string default_bootloader = FLAGS_system_image_dir + "/bootloader";
  SetCommandLineOptionWithMode("bootloader", default_bootloader.c_str(),
                               SET_FLAGS_DEFAULT);
}

void SetDefaultFlagsForGem5() {
  // TODO: Add support for gem5 gpu models
  SetCommandLineOptionWithMode("gpu_mode", kGpuModeGuestSwiftshader,
                               SET_FLAGS_DEFAULT);

  SetCommandLineOptionWithMode("cpus", "1", SET_FLAGS_DEFAULT);
}

Result<KernelConfig> GetKernelConfigAndSetDefaults() {
  CF_EXPECT(ResolveInstanceFiles(), "Failed to resolve instance files");

  KernelConfig kernel_config = CF_EXPECT(ReadKernelConfig());

  if (FLAGS_vm_manager == "") {
    if (IsHostCompatible(kernel_config.target_arch)) {
      FLAGS_vm_manager = CrosvmManager::name();
    } else {
      FLAGS_vm_manager = QemuManager::name();
    }
  }

  if (FLAGS_vm_manager == QemuManager::name()) {
    SetDefaultFlagsForQemu(kernel_config.target_arch);
  } else if (FLAGS_vm_manager == CrosvmManager::name()) {
    SetDefaultFlagsForCrosvm();
  } else if (FLAGS_vm_manager == Gem5Manager::name()) {
    // TODO: Get the other architectures working
    if (kernel_config.target_arch != Arch::Arm64) {
      return CF_ERR("Gem5 only supports ARM64");
    }
    SetDefaultFlagsForGem5();
  } else {
    return CF_ERR("Unknown Virtual Machine Manager: " << FLAGS_vm_manager);
  }
  if (FLAGS_vm_manager != Gem5Manager::name()) {
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

  return kernel_config;
}

std::string GetConfigFilePath(const CuttlefishConfig& config) {
  return config.AssemblyPath("cuttlefish_config.json");
}

std::string GetCuttlefishEnvPath() {
  return StringFromEnv("HOME", ".") + "/.cuttlefish.sh";
}

} // namespace cuttlefish
