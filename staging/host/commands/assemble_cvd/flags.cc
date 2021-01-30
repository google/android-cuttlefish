#include "host/commands/assemble_cvd/flags.h"

#include <android-base/logging.h>
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

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "host/commands/assemble_cvd/alloc.h"
#include "host/commands/assemble_cvd/boot_config.h"
#include "host/commands/assemble_cvd/boot_image_unpacker.h"
#include "host/commands/assemble_cvd/clean.h"
#include "host/commands/assemble_cvd/disk_flags.h"
#include "host/libs/config/fetcher_config.h"
#include "host/libs/config/host_tools_version.h"
#include "host/libs/graphics_detector/graphics_detector.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"
#include "host/libs/vm_manager/vm_manager.h"

using cuttlefish::DefaultHostArtifactsPath;
using cuttlefish::StringFromEnv;
using cuttlefish::vm_manager::CrosvmManager;
using google::FlagSettingMode::SET_FLAGS_DEFAULT;

DEFINE_string(config, "phone",
              "Config preset name. Will automatically set flag fields "
              "using the values from this file of presets. Possible values: "
              "phone,tablet,auto,tv");

DEFINE_int32(cpus, 2, "Virtual CPU count.");
DEFINE_string(data_policy, "use_existing", "How to handle userdata partition."
            " Either 'use_existing', 'create_if_missing', 'resize_up_to', or "
            "'always_create'.");
DEFINE_int32(blank_data_image_mb, 0,
             "The size of the blank data image to generate, MB.");
DEFINE_string(blank_data_image_fmt, "f2fs",
              "The fs format for the blank data image. Used with mkfs.");
DEFINE_string(qemu_gdb, "",
              "Debug flag to pass to qemu. e.g. -qemu_gdb=tcp::1234");

DEFINE_int32(x_res, 0, "Width of the screen in pixels");
DEFINE_int32(y_res, 0, "Height of the screen in pixels");
DEFINE_int32(dpi, 0, "Pixels per inch for the screen");
DEFINE_int32(refresh_rate_hz, 60, "Screen refresh rate in Hertz");
DEFINE_string(kernel_path, "",
              "Path to the kernel. Overrides the one from the boot image");
DEFINE_string(initramfs_path, "", "Path to the initramfs");
DEFINE_bool(decompress_kernel, false,
            "Whether to decompress the kernel image.");
DEFINE_string(extra_kernel_cmdline, "",
              "Additional flags to put on the kernel command line");
DEFINE_bool(guest_enforce_security, true,
            "Whether to run in enforcing mode (non permissive).");
DEFINE_bool(guest_audit_security, true,
            "Whether to log security audits.");
DEFINE_bool(guest_force_normal_boot, true,
            "Whether to force the boot sequence to skip recovery.");
DEFINE_int32(memory_mb, 0, "Total amount of memory available for guest, MB.");
DEFINE_string(serial_number, cuttlefish::ForCurrentInstance("CUTTLEFISHCVD"),
              "Serial number to use for the device");
DEFINE_bool(use_random_serial, false,
            "Whether to use random serial for the device.");
DEFINE_string(
    vm_manager, CrosvmManager::name(),
    "What virtual machine manager to use, one of {qemu_cli, crosvm}");
DEFINE_string(gpu_mode, cuttlefish::kGpuModeAuto,
              "What gpu configuration to use, one of {auto, drm_virgl, "
              "gfxstream, guest_swiftshader}");

DEFINE_bool(deprecated_boot_completed, false, "Log boot completed message to"
            " host kernel. This is only used during transition of our clients."
            " Will be deprecated soon.");
DEFINE_bool(start_vnc_server, false, "Whether to start the vnc server process. "
                                     "The VNC server runs at port 6443 + i for "
                                     "the vsoc-i user or CUTTLEFISH_INSTANCE=i, "
                                     "starting from 1.");
DEFINE_bool(use_allocd, false,
            "Acquire static resources from the resource allocator daemon.");
DEFINE_bool(enable_minimal_mode, false,
            "Only enable the minimum features to boot a cuttlefish device and "
            "support minimal UI interactions.\nNote: Currently only supports "
            "handheld/phone targets");
DEFINE_bool(pause_in_bootloader, false,
            "Stop the bootflow in u-boot. You can continue the boot by connecting "
            "to the device console and typing in \"boot\".");

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
    std::string("usr/share/crosvm/") + cuttlefish::HostArch() + "-linux-gnu/seccomp";
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

DEFINE_bool(
    start_webrtc_sig_server, false,
    "Whether to start the webrtc signaling server. This option only applies to "
    "the first instance, if multiple instances are launched they'll share the "
    "same signaling server, which is owned by the first one.");

DEFINE_string(webrtc_sig_server_addr, "0.0.0.0",
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

DEFINE_bool(verify_sig_server_certificate, false,
            "Whether to verify the signaling server's certificate with a "
            "trusted signing authority (Disallow self signed certificates).");

DEFINE_string(sig_server_headers_file, "",
              "Path to a file containing HTTP headers to be included in the "
              "connection to the signaling server. Each header should be on a "
              "line by itself in the form <name>: <value>");

DEFINE_string(
    webrtc_device_id, "cvd-{num}",
    "The for the device to register with the signaling server. Every "
    "appearance of the substring '{num}' in the device id will be substituted "
    "with the instance number to support multiple instances");

DEFINE_string(adb_mode, "vsock_half_tunnel",
              "Mode for ADB connection."
              "'vsock_tunnel' for a TCP connection tunneled through vsock, "
              "'native_vsock' for a  direct connection to the guest ADB over "
              "vsock, 'vsock_half_tunnel' for a TCP connection forwarded to "
              "the guest ADB server, or a comma separated list of types as in "
              "'native_vsock,vsock_half_tunnel'");
DEFINE_bool(run_adb_connector, !cuttlefish::IsRunningInContainer(),
            "Maintain adb connection by sending 'adb connect' commands to the "
            "server. Only relevant with -adb_mode=tunnel or vsock_tunnel");

DEFINE_string(uuid, cuttlefish::ForCurrentInstance(cuttlefish::kDefaultUuidPrefix),
              "UUID to use for the device. Random if not specified");
DEFINE_bool(daemon, false,
            "Run cuttlefish in background, the launcher exits on boot "
            "completed/failed");

DEFINE_string(device_title, "", "Human readable name for the instance, "
              "used by the vnc_server for its server title");
DEFINE_string(setupwizard_mode, "DISABLED",
            "One of DISABLED,OPTIONAL,REQUIRED");

DEFINE_string(qemu_binary,
              "/usr/bin/qemu-system-x86_64",
              "The qemu binary to use");
DEFINE_string(crosvm_binary, DefaultHostArtifactsPath("bin/crosvm"),
              "The Crosvm binary to use");
DEFINE_string(tpm_device, "", "A host TPM device to pass through commands to.");
DEFINE_bool(restart_subprocesses, true, "Restart any crashed host process");
DEFINE_bool(enable_vehicle_hal_grpc_server, true, "Enables the vehicle HAL "
            "emulation gRPC server on the host");
DEFINE_string(custom_action_config, "",
              "Path to a custom action config JSON. Defaults to the file provided by "
              "build variable CVD_CUSTOM_ACTION_CONFIG. If this build variable "
              "is empty then the custom action config will be empty as well.");
DEFINE_string(
    custom_actions, "",
    "Serialized JSON of an array of custom action objects (in the same format as custom "
    "action config JSON files). For use within --config preset config files; prefer "
    "--custom_action_config to specify a custom config file on the command line. "
    "--custom_action_config takes precedence over this flag if provided.");
DEFINE_bool(use_bootloader, true, "Boots the device using a bootloader");
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

DEFINE_bool(record_screen, false, "Enable screen recording. "
                                  "Requires --start_webrtc");

DEFINE_bool(ethernet, false, "Enable Ethernet network interface");

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

DECLARE_string(system_image_dir);

namespace cuttlefish {
using vm_manager::QemuManager;
using vm_manager::GetVmManager;

namespace {

const std::string kKernelDefaultPath = "kernel";
const std::string kInitramfsImg = "initramfs.img";
const std::string kRamdiskConcatExt = ".concat";

bool IsFlagSet(const std::string& flag) {
  return !gflags::GetCommandLineFlagInfoOrDie(flag.c_str()).is_default;
}

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

int NumStreamers() {
  auto start_flags = {FLAGS_start_vnc_server, FLAGS_start_webrtc};
  return std::count(start_flags.begin(), start_flags.end(), true);
}

std::string StrForInstance(const std::string& prefix, int num) {
  std::ostringstream stream;
  stream << prefix << std::setfill('0') << std::setw(2) << num;
  return stream.str();
}

bool ShouldEnableAcceleratedRendering(const GraphicsAvailability& availability) {
  return availability.has_egl &&
         availability.has_egl_surfaceless_with_gles &&
         availability.has_discrete_gpu;
}

// Runs GetGraphicsAvailability() inside of a subprocess to ensure that
// GetGraphicsAvailability() can complete successfully without crashing
// assemble_cvd. Configurations such as GCE instances without a GPU but with GPU
// drivers for example have seen crashes.
GraphicsAvailability GetGraphicsAvailabilityWithSubprocessCheck() {
  const std::string detect_graphics_bin =
      DefaultHostArtifactsPath("bin/detect_graphics");

  Command detect_graphics_cmd(detect_graphics_bin);

  SubprocessOptions detect_graphics_options;
  detect_graphics_options.Verbose(false);

  std::string detect_graphics_output;
  std::string detect_graphics_error;
  int ret = RunWithManagedStdio(std::move(detect_graphics_cmd),
                                nullptr,
                                &detect_graphics_output,
                                &detect_graphics_error,
                                detect_graphics_options);
  if (ret == 0) {
    return GetGraphicsAvailability();
  }
  LOG(VERBOSE) << "Subprocess for detect_graphics failed with "
               << ret
               << " : "
               << detect_graphics_output;
  return GraphicsAvailability{};
}

} // namespace

CuttlefishConfig InitializeCuttlefishConfiguration(
    const std::string& assembly_dir,
    const std::string& instance_dir,
    int modem_simulator_count,
    const BootImageUnpacker& boot_image_unpacker,
    const FetcherConfig& fetcher_config) {
  // At most one streamer can be started.
  CHECK(NumStreamers() <= 1);

  CuttlefishConfig tmp_config_obj;
  tmp_config_obj.set_assembly_dir(assembly_dir);
  auto vmm = GetVmManager(FLAGS_vm_manager);
  if (!vmm) {
    LOG(FATAL) << "Invalid vm_manager: " << FLAGS_vm_manager;
  }
  tmp_config_obj.set_vm_manager(FLAGS_vm_manager);

  const GraphicsAvailability graphics_availability =
    GetGraphicsAvailabilityWithSubprocessCheck();

  LOG(VERBOSE) << GetGraphicsAvailabilityString(graphics_availability);

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
  // Sepolicy rules need to be updated to support gpu mode. Temporarily disable
  // auto-enabling sandbox when gpu is enabled (b/152323505).
  if (tmp_config_obj.gpu_mode() != kGpuModeGuestSwiftshader) {
    SetCommandLineOptionWithMode("enable_sandbox", "false", SET_FLAGS_DEFAULT);
  }

  if (vmm->ConfigureGpuMode(tmp_config_obj.gpu_mode()).empty()) {
    LOG(FATAL) << "Invalid gpu_mode=" << FLAGS_gpu_mode <<
               " does not work with vm_manager=" << FLAGS_vm_manager;
  }

  tmp_config_obj.set_cpus(FLAGS_cpus);
  tmp_config_obj.set_memory_mb(FLAGS_memory_mb);

  tmp_config_obj.set_setupwizard_mode(FLAGS_setupwizard_mode);

  std::vector<cuttlefish::CuttlefishConfig::DisplayConfig> display_configs = {{
    .width = FLAGS_x_res,
    .height = FLAGS_y_res,
  }};
  tmp_config_obj.set_display_configs(display_configs);
  tmp_config_obj.set_dpi(FLAGS_dpi);
  tmp_config_obj.set_refresh_rate_hz(FLAGS_refresh_rate_hz);

  tmp_config_obj.set_gdb_flag(FLAGS_qemu_gdb);
  std::vector<std::string> adb = android::base::Split(FLAGS_adb_mode, ",");
  tmp_config_obj.set_adb_mode(std::set<std::string>(adb.begin(), adb.end()));
  std::string discovered_kernel = fetcher_config.FindCvdFileWithSuffix(kKernelDefaultPath);
  std::string foreign_kernel = FLAGS_kernel_path.size() ? FLAGS_kernel_path : discovered_kernel;
  if (foreign_kernel.size()) {
    tmp_config_obj.set_kernel_image_path(foreign_kernel);
    tmp_config_obj.set_use_unpacked_kernel(false);
  } else {
    tmp_config_obj.set_kernel_image_path(
        tmp_config_obj.AssemblyPath(kKernelDefaultPath.c_str()));
    tmp_config_obj.set_use_unpacked_kernel(true);
  }

  tmp_config_obj.set_decompress_kernel(FLAGS_decompress_kernel);
  if (tmp_config_obj.decompress_kernel()) {
    tmp_config_obj.set_decompressed_kernel_image_path(
        tmp_config_obj.AssemblyPath("vmlinux"));
  }

  auto ramdisk_path = tmp_config_obj.AssemblyPath("ramdisk.img");
  auto vendor_ramdisk_path = tmp_config_obj.AssemblyPath("vendor_ramdisk.img");
  if (!boot_image_unpacker.HasRamdiskImage()) {
    LOG(FATAL) << "A ramdisk is required, but the boot image did not have one.";
  }

  std::string discovered_ramdisk = fetcher_config.FindCvdFileWithSuffix(kInitramfsImg);
  std::string foreign_ramdisk = FLAGS_initramfs_path.size () ? FLAGS_initramfs_path : discovered_ramdisk;

  tmp_config_obj.set_boot_image_kernel_cmdline(boot_image_unpacker.kernel_cmdline());
  tmp_config_obj.set_guest_enforce_security(FLAGS_guest_enforce_security);
  tmp_config_obj.set_guest_audit_security(FLAGS_guest_audit_security);
  tmp_config_obj.set_guest_force_normal_boot(FLAGS_guest_force_normal_boot);
  tmp_config_obj.set_extra_kernel_cmdline(FLAGS_extra_kernel_cmdline);

  if (FLAGS_console) {
    SetCommandLineOptionWithMode("enable_sandbox", "false", SET_FLAGS_DEFAULT);
  }

  tmp_config_obj.set_console(FLAGS_console);
  tmp_config_obj.set_kgdb(FLAGS_console && FLAGS_kgdb);

  tmp_config_obj.set_ramdisk_image_path(ramdisk_path);
  tmp_config_obj.set_vendor_ramdisk_image_path(vendor_ramdisk_path);

  if (foreign_kernel.size() && !foreign_ramdisk.size()) {
    // If there's a kernel that's passed in without an initramfs, that implies
    // user error or a kernel built with no modules. In either case, let's
    // choose to avoid loading the modules from the vendor ramdisk which are
    // built for the default cf kernel. Once boot occurs, user error will
    // become obvious.
    tmp_config_obj.set_final_ramdisk_path(ramdisk_path);
  } else {
    tmp_config_obj.set_final_ramdisk_path(ramdisk_path + kRamdiskConcatExt);
    if(foreign_ramdisk.size()) {
      tmp_config_obj.set_initramfs_path(foreign_ramdisk);
    }
  }

  tmp_config_obj.set_host_tools_version(HostToolsCrc());

  tmp_config_obj.set_deprecated_boot_completed(FLAGS_deprecated_boot_completed);

  tmp_config_obj.set_qemu_binary(FLAGS_qemu_binary);
  tmp_config_obj.set_crosvm_binary(FLAGS_crosvm_binary);
  tmp_config_obj.set_tpm_device(FLAGS_tpm_device);

  tmp_config_obj.set_enable_vnc_server(FLAGS_start_vnc_server);

  tmp_config_obj.set_seccomp_policy_dir(FLAGS_seccomp_policy_dir);

  tmp_config_obj.set_enable_webrtc(FLAGS_start_webrtc);
  tmp_config_obj.set_webrtc_assets_dir(FLAGS_webrtc_assets_dir);
  tmp_config_obj.set_webrtc_certs_dir(FLAGS_webrtc_certs_dir);
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

  tmp_config_obj.set_restart_subprocesses(FLAGS_restart_subprocesses);
  tmp_config_obj.set_run_adb_connector(FLAGS_run_adb_connector);
  tmp_config_obj.set_run_as_daemon(FLAGS_daemon);

  tmp_config_obj.set_data_policy(FLAGS_data_policy);
  tmp_config_obj.set_blank_data_image_mb(FLAGS_blank_data_image_mb);
  tmp_config_obj.set_blank_data_image_fmt(FLAGS_blank_data_image_fmt);

  tmp_config_obj.set_enable_gnss_grpc_proxy(FLAGS_start_gnss_proxy);

  tmp_config_obj.set_enable_vehicle_hal_grpc_server(FLAGS_enable_vehicle_hal_grpc_server);
  tmp_config_obj.set_vehicle_hal_grpc_server_binary(
      DefaultHostArtifactsPath("bin/android.hardware.automotive.vehicle@2.0-virtualization-grpc-server"));

  std::string custom_action_config;
  if (!FLAGS_custom_action_config.empty()) {
    custom_action_config = FLAGS_custom_action_config;
  } else {
    std::string custom_action_config_dir =
        DefaultHostArtifactsPath("etc/cvd_custom_action_config");
    if (DirectoryExists(custom_action_config_dir)) {
      auto custom_action_configs = DirectoryContents(custom_action_config_dir);
      // Two entries are always . and ..
      if (custom_action_configs.size() > 3) {
        LOG(ERROR) << "Expected at most one custom action config in "
                   << custom_action_config_dir << ". Please delete extras.";
      } else if (custom_action_configs.size() == 3) {
        for (const auto& config : custom_action_configs) {
          if (android::base::EndsWithIgnoreCase(config, ".json")) {
            custom_action_config = custom_action_config_dir + "/" + config;
          }
        }
      }
    }
  }
  Json::Reader reader;
  Json::Value custom_action_array(Json::arrayValue);
  if (custom_action_config != "") {
    // Load the custom action config JSON.
    std::ifstream ifs(custom_action_config);
    if (!reader.parse(ifs, custom_action_array)) {
      LOG(FATAL) << "Could not read custom actions config file "
                 << custom_action_config << ": "
                 << reader.getFormattedErrorMessages();
    }
  } else if (FLAGS_custom_actions != "") {
    // Load the custom action from the --config preset file.
    if (!reader.parse(FLAGS_custom_actions, custom_action_array)) {
      LOG(FATAL) << "Could not read custom actions config flag: "
                 << reader.getFormattedErrorMessages();
    }
  }
  std::vector<CustomActionConfig> custom_actions;
  for (Json::Value custom_action : custom_action_array) {
    custom_actions.push_back(CustomActionConfig(custom_action));
  }
  tmp_config_obj.set_custom_actions(custom_actions);

  tmp_config_obj.set_use_bootloader(FLAGS_use_bootloader);
  tmp_config_obj.set_bootloader(FLAGS_bootloader);

  tmp_config_obj.set_enable_metrics(FLAGS_report_anonymous_usage_stats);

  if (!FLAGS_boot_slot.empty()) {
      tmp_config_obj.set_boot_slot(FLAGS_boot_slot);
  }

  tmp_config_obj.set_cuttlefish_env_path(GetCuttlefishEnvPath());

  tmp_config_obj.set_ril_dns(FLAGS_ril_dns);

  tmp_config_obj.set_enable_minimal_mode(FLAGS_enable_minimal_mode);

  tmp_config_obj.set_vhost_net(FLAGS_vhost_net);
  tmp_config_obj.set_record_screen(FLAGS_record_screen);

  tmp_config_obj.set_ethernet(FLAGS_ethernet);

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
        const_cast<const CuttlefishConfig&>(tmp_config_obj)
            .ForInstance(num);
    // Set this first so that calls to PerInstancePath below are correct
    instance.set_instance_dir(instance_dir + "." + std::to_string(num));
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
      // a base (vsock) port is like 9200 for modem_simulator, etc
      return cuttlefish::GetVsockServerPort(base_port, vsock_guest_cid);
    };
    instance.set_session_id(iface_config.mobile_tap.session_id);

    instance.set_mobile_bridge_name(StrForInstance("cvd-mbr-", num));
    instance.set_mobile_tap_name(iface_config.mobile_tap.name);
    instance.set_wifi_tap_name(iface_config.wireless_tap.name);
    instance.set_ethernet_tap_name(iface_config.ethernet_tap.name);

    instance.set_uuid(FLAGS_uuid);

    instance.set_vnc_server_port(6444 + num - 1);
    instance.set_host_port(6520 + num - 1);
    instance.set_adb_ip_and_port("0.0.0.0:" + std::to_string(6520 + num - 1));
    instance.set_tombstone_receiver_port(calc_vsock_port(6600));
    instance.set_vehicle_hal_server_port(9210 + num - 1);
    instance.set_audiocontrol_server_port(9410);  /* OK to use the same port number across instances */
    instance.set_config_server_port(calc_vsock_port(6800));

    if (FLAGS_gpu_mode != kGpuModeDrmVirgl &&
        FLAGS_gpu_mode != kGpuModeGfxStream) {
        instance.set_frames_server_port(calc_vsock_port(6900));
      if (FLAGS_vm_manager == QemuManager::name()) {
        instance.set_keyboard_server_port(calc_vsock_port(7000));
        instance.set_touch_server_port(calc_vsock_port(7100));
      }
    }

    instance.set_gnss_grpc_proxy_server_port(7200 + num -1);

    if (num <= gnss_file_paths.size()) {
      instance.set_gnss_file_path(gnss_file_paths[num-1]);
    }

    instance.set_device_title(FLAGS_device_title);

    instance.set_virtual_disk_paths({
      const_instance.PerInstancePath("overlay.img"),
      const_instance.sdcard_path(),
      const_instance.factory_reset_protected_path(),
    });

    std::array<unsigned char, 6> mac_address;
    mac_address[0] = 1 << 6; // locally administered
    // TODO(schuffelen): Randomize these and preserve the state.
    for (int i = 1; i < 5; i++) {
      mac_address[i] = i;
    }
    mac_address[5] = num;
    instance.set_wifi_mac_address(mac_address);

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
    if (FLAGS_start_webrtc_sig_server && is_first_instance) {
      auto port = 8443 + num - 1;
      // Change the signaling server port for all instances
      tmp_config_obj.set_sig_server_port(port);
      instance.set_start_webrtc_signaling_server(true);
    } else {
      instance.set_start_webrtc_signaling_server(false);
    }
    is_first_instance = false;

    // instance.modem_simulator_ports := "" or "[port,]*port"
    if (modem_simulator_count > 0) {
      std::stringstream modem_ports;
      for (auto index {0}; index < modem_simulator_count - 1; index++) {
        modem_ports << calc_vsock_port(9200) << ",";
      }
      modem_ports << calc_vsock_port(9200);
      instance.set_modem_simulator_ports(modem_ports.str());
    } else {
      instance.set_modem_simulator_ports("");
    }
  } // end of num_instances loop

  tmp_config_obj.set_enable_sandbox(FLAGS_enable_sandbox);

  return tmp_config_obj;
}

void SetDefaultFlagsFromConfigPreset() {
  std::string config_preset = FLAGS_config;  // The name of the preset config.
  std::string config_file_path;  // The path to the preset config JSON.
  const std::set<std::string> allowed_config_presets = {
      "phone",
      "tablet",
      "tv",
      "auto",
  };

  // If the user specifies a --config name, then use that config
  // preset option and save their choice to a file.
  std::string config_preset_file_path =
      StringFromEnv("HOME", ".") + "/.cuttlefish_config_preset";
  if (IsFlagSet("config")) {
    if (!allowed_config_presets.count(config_preset)) {
      LOG(FATAL) << "Invalid --config option '" << config_preset
                 << "'. Valid options: "
                 << android::base::Join(allowed_config_presets, ",");
    }
    // Write the name of the config preset to a file. Only the name is
    // written, not the contents of the config itself, in order to allow
    // forwards compatibility if config fields change.
    std::ofstream ofs(config_preset_file_path);
    if (ofs.is_open()) {
      ofs << config_preset;
    }
  } else if (FileExists(config_preset_file_path)) {
    // Load the config preset option from the file if it exists.
    std::ifstream ifs(config_preset_file_path);
    if (ifs.is_open()) {
      ifs >> config_preset;
      if (!allowed_config_presets.count(config_preset)) {
        LOG(WARNING) << config_preset_file_path
                     << " contains invalid config preset: '" << config_preset
                     << "'. Defaulting to 'phone'.";
        config_preset = "phone";
      }
    }
  }
  LOG(INFO) << "Launching CVD using --config='" << config_preset << "'.";

  config_file_path = DefaultHostArtifactsPath("etc/cvd_config/cvd_config_" +
                                              config_preset + ".json");
  Json::Value config;
  Json::Reader config_reader;
  std::ifstream ifs(config_file_path);
  if (!config_reader.parse(ifs, config)) {
    LOG(FATAL) << "Could not read config file " << config_file_path << ": "
               << config_reader.getFormattedErrorMessages();
  }
  for (const std::string& flag : config.getMemberNames()) {
    std::string value;
    if (flag == "custom_actions") {
      Json::FastWriter writer;
      value = writer.write(config[flag]);
    } else {
      value = config[flag].asString();
    }
    if (gflags::SetCommandLineOptionWithMode(flag.c_str(), value.c_str(),
                                             SET_FLAGS_DEFAULT)
            .empty()) {
      LOG(FATAL) << "Error setting flag '" << flag << "'.";
    }
  }
}

void SetDefaultFlagsForQemu() {
  // for now, we don't set non-default options for QEMU
  if (FLAGS_gpu_mode == kGpuModeGuestSwiftshader && NumStreamers() == 0) {
    // This makes WebRTC the default streamer unless the user requests
    // another via a --star_<streamer> flag, while at the same time it's
    // possible to run without any streamer by setting --start_webrtc=false.
    SetCommandLineOptionWithMode("start_webrtc", "true", SET_FLAGS_DEFAULT);
  }
  std::string default_bootloader = FLAGS_system_image_dir + "/bootloader.qemu";
  SetCommandLineOptionWithMode("bootloader", default_bootloader.c_str(),
                               SET_FLAGS_DEFAULT);
}

void SetDefaultFlagsForCrosvm() {
  if (NumStreamers() == 0) {
    // This makes WebRTC the default streamer unless the user requests
    // another via a --star_<streamer> flag, while at the same time it's
    // possible to run without any streamer by setting --start_webrtc=false.
    SetCommandLineOptionWithMode("start_webrtc", "true", SET_FLAGS_DEFAULT);
  }

  // for now, we support only x86_64 by default
  bool default_enable_sandbox = false;
  std::set<const std::string> supported_archs{std::string("x86_64")};
  if (supported_archs.find(HostArch()) != supported_archs.end()) {
    if (DirectoryExists(kCrosvmVarEmptyDir)) {
      default_enable_sandbox = IsDirectoryEmpty(kCrosvmVarEmptyDir);
    } else if (FileExists(kCrosvmVarEmptyDir)) {
      default_enable_sandbox = false;
    } else {
      default_enable_sandbox = EnsureDirectoryExists(kCrosvmVarEmptyDir);
    }
  }

  SetCommandLineOptionWithMode("enable_sandbox",
                               (default_enable_sandbox ? "true" : "false"),
                               SET_FLAGS_DEFAULT);

  std::string default_bootloader = FLAGS_system_image_dir + "/bootloader";
  SetCommandLineOptionWithMode("bootloader", default_bootloader.c_str(),
                               SET_FLAGS_DEFAULT);
}

bool ParseCommandLineFlags(int* argc, char*** argv) {
  google::ParseCommandLineNonHelpFlags(argc, argv, true);
  SetDefaultFlagsFromConfigPreset();
  bool invalid_manager = false;
  if (FLAGS_vm_manager == QemuManager::name()) {
    SetDefaultFlagsForQemu();
  } else if (FLAGS_vm_manager == CrosvmManager::name()) {
    SetDefaultFlagsForCrosvm();
  } else {
    std::cerr << "Unknown Virtual Machine Manager: " << FLAGS_vm_manager
              << std::endl;
    invalid_manager = true;
  }
  // The default for starting signaling server is whether or not webrt is to be
  // started.
  SetCommandLineOptionWithMode("start_webrtc_sig_server",
                               FLAGS_start_webrtc ? "true" : "false",
                               SET_FLAGS_DEFAULT);
  google::HandleCommandLineHelpFlags();
  if (invalid_manager) {
    return false;
  }
  // Set the env variable to empty (in case the caller passed a value for it).
  unsetenv(kCuttlefishConfigEnvVarName);

  return ResolveInstanceFiles();
}

std::string GetConfigFilePath(const CuttlefishConfig& config) {
  return config.AssemblyPath("cuttlefish_config.json");
}

std::string GetCuttlefishEnvPath() {
  return StringFromEnv("HOME", ".") + "/.cuttlefish.sh";
}

} // namespace cuttlefish
