#include "host/commands/launch/flags.h"

#include <iostream>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/strings/str_split.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/vsoc/lib/vsoc_memory.h"
#include "host/commands/launch/boot_image_unpacker.h"
#include "host/commands/launch/data_image.h"
#include "host/commands/launch/launch.h"
#include "host/commands/launch/launcher_defs.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"
#include "host/libs/vm_manager/vm_manager.h"

using vsoc::GetPerInstanceDefault;
using cvd::LauncherExitCodes;

DEFINE_string(
    system_image, "",
    "Path to the system image, if empty it is assumed to be a file named "
    "system.img in the directory specified by -system_image_dir");
DEFINE_string(cache_image, "", "Location of the cache partition image.");
DEFINE_string(metadata_image, "", "Location of the metadata partition image "
              "to be generated.");
DEFINE_int32(blank_metadata_image_mb, 16,
             "The size of the blank metadata image to generate, MB.");
DEFINE_int32(cpus, 2, "Virtual CPU count.");
DEFINE_string(data_image, "", "Location of the data partition image.");
DEFINE_string(data_policy, "use_existing", "How to handle userdata partition."
            " Either 'use_existing', 'create_if_missing', 'resize_up_to', or "
            "'always_create'.");
DEFINE_int32(blank_data_image_mb, 0,
             "The size of the blank data image to generate, MB.");
DEFINE_string(blank_data_image_fmt, "ext4",
              "The fs format for the blank data image. Used with mkfs.");
DEFINE_string(qemu_gdb, "",
              "Debug flag to pass to qemu. e.g. -qemu_gdb=tcp::1234");

DEFINE_int32(x_res, 720, "Width of the screen in pixels");
DEFINE_int32(y_res, 1280, "Height of the screen in pixels");
DEFINE_int32(dpi, 160, "Pixels per inch for the screen");
DEFINE_int32(refresh_rate_hz, 60, "Screen refresh rate in Hertz");
DEFINE_int32(num_screen_buffers, 3, "The number of screen buffers");
DEFINE_string(kernel_path, "",
              "Path to the kernel. Overrides the one from the boot image");
DEFINE_bool(decompress_kernel, false,
            "Whether to decompress the kernel image. Required for crosvm.");
DEFINE_string(kernel_decompresser_executable,
              vsoc::DefaultHostArtifactsPath("bin/extract-vmlinux"),
             "Path to the extract-vmlinux executable.");
DEFINE_string(extra_kernel_cmdline, "",
              "Additional flags to put on the kernel command line");
DEFINE_int32(loop_max_part, 7, "Maximum number of loop partitions");
DEFINE_string(console, "ttyS0", "Console device for the guest kernel.");
DEFINE_string(androidboot_console, "ttyS1",
              "Console device for the Android framework");
DEFINE_string(
    hardware_name, "",
    "The codename of the device's hardware, one of {cutf_ivsh, cutf_cvm}");
DEFINE_string(guest_security, "selinux",
              "The security module to use in the guest");
DEFINE_bool(guest_enforce_security, true,
            "Whether to run in enforcing mode (non permissive). Ignored if "
            "-guest_security is empty.");
DEFINE_bool(guest_audit_security, true,
            "Whether to log security audits.");
DEFINE_string(boot_image, "", "Location of cuttlefish boot image.");
DEFINE_int32(memory_mb, 2048,
             "Total amount of memory available for guest, MB.");
std::string g_default_mempath{vsoc::GetDefaultMempath()};
DEFINE_string(mempath, g_default_mempath.c_str(),
              "Target location for the shmem file.");
DEFINE_string(mobile_interface, "", // default handled on ParseCommandLine
              "Network interface to use for mobile networking");
DEFINE_string(mobile_tap_name, "", // default handled on ParseCommandLine
              "The name of the tap interface to use for mobile");
std::string g_default_serial_number{GetPerInstanceDefault("CUTTLEFISHCVD")};
DEFINE_string(serial_number, g_default_serial_number.c_str(),
              "Serial number to use for the device");
DEFINE_string(instance_dir, "", // default handled on ParseCommandLine
              "A directory to put all instance specific files");
DEFINE_string(
    vm_manager, vm_manager::CrosvmManager::name(),
    "What virtual machine manager to use, one of {qemu_cli, crosvm}");
DEFINE_string(
    gpu_mode, vsoc::kGpuModeGuestSwiftshader,
    "What gpu configuration to use, one of {guest_swiftshader, drm_virgl}");
DEFINE_string(wayland_socket, "",
    "Location of the wayland socket to use for drm_virgl gpu_mode.");

DEFINE_string(system_image_dir, vsoc::DefaultGuestImagePath(""),
              "Location of the system partition images.");
DEFINE_string(vendor_image, "", "Location of the vendor partition image.");
DEFINE_string(product_image, "", "Location of the product partition image.");
DEFINE_string(super_image, "", "Location of the super partition image.");

DEFINE_bool(deprecated_boot_completed, false, "Log boot completed message to"
            " host kernel. This is only used during transition of our clients."
            " Will be deprecated soon.");
DEFINE_bool(start_vnc_server, true, "Whether to start the vnc server process.");
DEFINE_string(vnc_server_binary,
              vsoc::DefaultHostArtifactsPath("bin/vnc_server"),
              "Location of the vnc server binary.");
DEFINE_bool(start_stream_audio, false,
            "Whether to start the stream audio process.");
DEFINE_string(stream_audio_binary,
              vsoc::DefaultHostArtifactsPath("bin/stream_audio"),
              "Location of the stream_audio binary.");
DEFINE_string(virtual_usb_manager_binary,
              vsoc::DefaultHostArtifactsPath("bin/virtual_usb_manager"),
              "Location of the virtual usb manager binary.");
DEFINE_string(kernel_log_monitor_binary,
              vsoc::DefaultHostArtifactsPath("bin/kernel_log_monitor"),
              "Location of the log monitor binary.");
DEFINE_string(ivserver_binary,
              vsoc::DefaultHostArtifactsPath("bin/ivserver"),
              "Location of the ivshmem server binary.");
DEFINE_int32(vnc_server_port, GetPerInstanceDefault(6444),
             "The port on which the vnc server should listen");
DEFINE_int32(stream_audio_port, GetPerInstanceDefault(7444),
             "The port on which stream_audio should listen.");
DEFINE_string(socket_forward_proxy_binary,
              vsoc::DefaultHostArtifactsPath("bin/socket_forward_proxy"),
              "Location of the socket_forward_proxy binary.");
DEFINE_string(socket_vsock_proxy_binary,
              vsoc::DefaultHostArtifactsPath("bin/socket_vsock_proxy"),
              "Location of the socket_vsock_proxy binary.");
DEFINE_string(adb_mode, "vsock_half_tunnel",
              "Mode for ADB connection. Can be 'usb' for USB forwarding, "
              "'tunnel' for a TCP connection tunneled through VSoC, "
              "'vsock_tunnel' for a TCP connection tunneled through vsock, "
              "'native_vsock' for a  direct connection to the guest ADB over "
              "vsock, 'vsock_half_tunnel' for a TCP connection forwarded to "
              "the guest ADB server, or a comma separated list of types as in "
              "'usb,tunnel'");
DEFINE_bool(run_adb_connector, true,
            "Maintain adb connection by sending 'adb connect' commands to the "
            "server. Only relevant with -adb_mode=tunnel or vsock_tunnel");
DEFINE_string(adb_connector_binary,
              vsoc::DefaultHostArtifactsPath("bin/adb_connector"),
              "Location of the adb_connector binary. Only relevant if "
              "-run_adb_connector is true");
DEFINE_int32(vhci_port, GetPerInstanceDefault(0), "VHCI port to use for usb");
DEFINE_string(guest_mac_address,
              GetPerInstanceDefault("00:43:56:44:80:"), // 00:43:56:44:80:0x
              "MAC address of the wifi interface to be created on the guest.");
DEFINE_string(host_mac_address,
              "42:00:00:00:00:00",
              "MAC address of the wifi interface running on the host.");
DEFINE_string(wifi_tap_name, "", // default handled on ParseCommandLine
              "The name of the tap interface to use for wifi");
DEFINE_int32(vsock_guest_cid,
             vsoc::GetDefaultPerInstanceVsockCid(),
             "Guest identifier for vsock. Disabled if under 3.");

// TODO(b/72969289) This should be generated
DEFINE_string(dtb, "", "Path to the cuttlefish.dtb file");
DEFINE_string(gsi_fstab,
              vsoc::DefaultHostArtifactsPath("config/gsi.fstab"),
              "Path to the GSI fstab file");

DEFINE_string(uuid, vsoc::GetPerInstanceDefault(vsoc::kDefaultUuidPrefix),
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
DEFINE_string(crosvm_binary,
              vsoc::DefaultHostArtifactsPath("bin/crosvm"),
              "The Crosvm binary to use");
DEFINE_bool(restart_subprocesses, true, "Restart any crashed host process");
DEFINE_bool(run_e2e_test, true, "Run e2e test after device launches");
DEFINE_string(e2e_test_binary,
              vsoc::DefaultHostArtifactsPath("bin/host_region_e2e_test"),
              "Location of the region end to end test binary");
DEFINE_string(logcat_receiver_binary,
              vsoc::DefaultHostArtifactsPath("bin/logcat_receiver"),
              "Binary for the logcat server");
DEFINE_string(logcat_mode, "", "How to send android's log messages from "
                               "guest to host. One of [serial, vsock]");
DEFINE_int32(logcat_vsock_port, vsoc::GetPerInstanceDefault(5620),
             "The port for logcat over vsock");
DEFINE_string(config_server_binary,
              vsoc::DefaultHostArtifactsPath("bin/config_server"),
              "Binary for the configuration server");
DEFINE_int32(config_server_port, vsoc::GetPerInstanceDefault(4680),
             "The (vsock) port for the configuration server");
DEFINE_int32(frames_vsock_port, vsoc::GetPerInstanceDefault(5580),
             "The vsock port to receive frames from the guest on");
DEFINE_bool(enable_tombstone_receiver, false, "Enables the tombstone logger on "
            "both the guest and the host");
DEFINE_string(tombstone_receiver_binary,
              vsoc::DefaultHostArtifactsPath("bin/tombstone_receiver"),
              "Binary for the tombstone server");
DEFINE_int32(tombstone_receiver_port, vsoc::GetPerInstanceDefault(5630),
             "The vsock port for tombstones");
namespace {

template<typename S, typename T>
static std::string concat(const S& s, const T& t) {
  std::ostringstream os;
  os << s << t;
  return os.str();
}

bool ResolveInstanceFiles() {
  if (FLAGS_system_image_dir.empty()) {
    LOG(ERROR) << "--system_image_dir must be specified.";
    return false;
  }

  // If user did not specify location of either of these files, expect them to
  // be placed in --system_image_dir location.
  std::string default_system_image = FLAGS_system_image_dir + "/system.img";
  SetCommandLineOptionWithMode("system_image", default_system_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_boot_image = FLAGS_system_image_dir + "/boot.img";
  SetCommandLineOptionWithMode("boot_image", default_boot_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_cache_image = FLAGS_system_image_dir + "/cache.img";
  SetCommandLineOptionWithMode("cache_image", default_cache_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_data_image = FLAGS_system_image_dir + "/userdata.img";
  SetCommandLineOptionWithMode("data_image", default_data_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_vendor_image = FLAGS_system_image_dir + "/vendor.img";
  SetCommandLineOptionWithMode("vendor_image", default_vendor_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_metadata_image = FLAGS_system_image_dir + "/metadata.img";
  SetCommandLineOptionWithMode("metadata_image", default_metadata_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_product_image = FLAGS_system_image_dir + "/product.img";
  SetCommandLineOptionWithMode("product_image", default_product_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_super_image = FLAGS_system_image_dir + "/super.img";
  SetCommandLineOptionWithMode("super_image", default_super_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);

  return true;
}

std::string GetCuttlefishEnvPath() {
  return cvd::StringFromEnv("HOME", ".") + "/.cuttlefish.sh";
}

// Initializes the config object and saves it to file. It doesn't return it, all
// further uses of the config should happen through the singleton
bool InitializeCuttlefishConfiguration(
    const cvd::BootImageUnpacker& boot_image_unpacker) {
  vsoc::CuttlefishConfig tmp_config_obj;
  auto& memory_layout = *vsoc::VSoCMemoryLayout::Get();
  // Set this first so that calls to PerInstancePath below are correct
  tmp_config_obj.set_instance_dir(FLAGS_instance_dir);
  if (!vm_manager::VmManager::IsValidName(FLAGS_vm_manager)) {
    LOG(ERROR) << "Invalid vm_manager: " << FLAGS_vm_manager;
    return false;
  }
  if (!vm_manager::VmManager::IsValidName(FLAGS_vm_manager)) {
    LOG(ERROR) << "Invalid vm_manager: " << FLAGS_vm_manager;
    return false;
  }
  tmp_config_obj.set_vm_manager(FLAGS_vm_manager);
  tmp_config_obj.set_gpu_mode(FLAGS_gpu_mode);
  if (!vm_manager::VmManager::ConfigureGpuMode(&tmp_config_obj)) {
    LOG(ERROR) << "Invalid gpu_mode=" << FLAGS_gpu_mode <<
               " does not work with vm_manager=" << FLAGS_vm_manager;
    return false;
  }
  tmp_config_obj.set_wayland_socket(FLAGS_wayland_socket);

  vm_manager::VmManager::ConfigureBootDevices(&tmp_config_obj);

  tmp_config_obj.set_serial_number(FLAGS_serial_number);

  tmp_config_obj.set_cpus(FLAGS_cpus);
  tmp_config_obj.set_memory_mb(FLAGS_memory_mb);

  tmp_config_obj.set_dpi(FLAGS_dpi);
  tmp_config_obj.set_setupwizard_mode(FLAGS_setupwizard_mode);
  tmp_config_obj.set_x_res(FLAGS_x_res);
  tmp_config_obj.set_y_res(FLAGS_y_res);
  tmp_config_obj.set_num_screen_buffers(FLAGS_num_screen_buffers);
  tmp_config_obj.set_refresh_rate_hz(FLAGS_refresh_rate_hz);
  tmp_config_obj.set_gdb_flag(FLAGS_qemu_gdb);
  std::vector<std::string> adb = cvd::StrSplit(FLAGS_adb_mode, ',');
  tmp_config_obj.set_adb_mode(std::set<std::string>(adb.begin(), adb.end()));
  tmp_config_obj.set_adb_ip_and_port("127.0.0.1:" + std::to_string(GetHostPort()));

  tmp_config_obj.set_device_title(FLAGS_device_title);
  if (FLAGS_kernel_path.size()) {
    tmp_config_obj.set_kernel_image_path(FLAGS_kernel_path);
    tmp_config_obj.set_use_unpacked_kernel(false);
  } else {
    tmp_config_obj.set_kernel_image_path(
        tmp_config_obj.PerInstancePath("kernel"));
    tmp_config_obj.set_use_unpacked_kernel(true);
  }
  tmp_config_obj.set_decompress_kernel(FLAGS_decompress_kernel);
  if (FLAGS_decompress_kernel) {
    tmp_config_obj.set_decompressed_kernel_image_path(
        tmp_config_obj.PerInstancePath("vmlinux"));
  }

  auto ramdisk_path = tmp_config_obj.PerInstancePath("ramdisk.img");
  bool use_ramdisk = boot_image_unpacker.HasRamdiskImage();
  if (!use_ramdisk) {
    LOG(INFO) << "No ramdisk present; assuming system-as-root build";
    ramdisk_path = "";
  }

  // Fallback for older builds, or builds from branches without DAP
  if (!FLAGS_super_image.empty() && !cvd::FileHasContent(FLAGS_super_image.c_str())) {
    LOG(INFO) << "No super image detected; assuming non-DAP build";
    FLAGS_super_image.clear();
  }

  // This needs to be done here because the dtb path depends on the presence of
  // the ramdisk. If we are booting a super image, the fstab is passed through
  // from the ramdisk, it should never be defined by dt.
  if (FLAGS_super_image.empty() && FLAGS_dtb.empty()) {
    if (use_ramdisk) {
      FLAGS_dtb = vsoc::DefaultHostArtifactsPath("config/initrd-root.dtb");
    } else {
      FLAGS_dtb = vsoc::DefaultHostArtifactsPath("config/system-root.dtb");
    }
  }

  tmp_config_obj.add_kernel_cmdline(boot_image_unpacker.kernel_cmdline());
  if (!use_ramdisk) {
    tmp_config_obj.add_kernel_cmdline("root=/dev/vda");
  }
  if (!FLAGS_super_image.empty()) {
    tmp_config_obj.add_kernel_cmdline("androidboot.super_partition=vda");
  }
  tmp_config_obj.add_kernel_cmdline("init=/init");
  tmp_config_obj.add_kernel_cmdline(
      concat("androidboot.serialno=", FLAGS_serial_number));
  tmp_config_obj.add_kernel_cmdline("mac80211_hwsim.radios=0");
  tmp_config_obj.add_kernel_cmdline(concat("androidboot.lcd_density=", FLAGS_dpi));
  tmp_config_obj.add_kernel_cmdline(
      concat("androidboot.setupwizard_mode=", FLAGS_setupwizard_mode));
  tmp_config_obj.add_kernel_cmdline(concat("loop.max_part=", FLAGS_loop_max_part));
  if (!FLAGS_console.empty()) {
    tmp_config_obj.add_kernel_cmdline(concat("console=", FLAGS_console));
  }
  if (!FLAGS_androidboot_console.empty()) {
    tmp_config_obj.add_kernel_cmdline(
        concat("androidboot.console=", FLAGS_androidboot_console));
  }
  if (!FLAGS_hardware_name.empty()) {
    tmp_config_obj.add_kernel_cmdline(
        concat("androidboot.hardware=", FLAGS_hardware_name));
  }
  if (FLAGS_logcat_mode == cvd::kLogcatVsockMode) {
    tmp_config_obj.add_kernel_cmdline(concat("androidboot.vsock_logcat_port=",
                                             FLAGS_logcat_vsock_port));
  }
  tmp_config_obj.add_kernel_cmdline(concat("androidboot.cuttlefish_config_server_port=",
                                           FLAGS_config_server_port));
  tmp_config_obj.set_hardware_name(FLAGS_hardware_name);
  if (!FLAGS_guest_security.empty()) {
    tmp_config_obj.add_kernel_cmdline(concat("security=", FLAGS_guest_security));
    if (FLAGS_guest_enforce_security) {
      tmp_config_obj.add_kernel_cmdline("enforcing=1");
    } else {
      tmp_config_obj.add_kernel_cmdline("enforcing=0");
      tmp_config_obj.add_kernel_cmdline("androidboot.selinux=permissive");
    }
    if (FLAGS_guest_audit_security) {
      tmp_config_obj.add_kernel_cmdline("audit=1");
    } else {
      tmp_config_obj.add_kernel_cmdline("audit=0");
    }
  }
  if (FLAGS_run_e2e_test) {
    tmp_config_obj.add_kernel_cmdline("androidboot.vsoc_e2e_test=1");
  }
  if (FLAGS_extra_kernel_cmdline.size()) {
    tmp_config_obj.add_kernel_cmdline(FLAGS_extra_kernel_cmdline);
  }

  if (FLAGS_super_image.empty()) {
    tmp_config_obj.set_system_image_path(FLAGS_system_image);
    tmp_config_obj.set_vendor_image_path(FLAGS_vendor_image);
    tmp_config_obj.set_product_image_path(FLAGS_product_image);
    tmp_config_obj.set_super_image_path("");
    tmp_config_obj.set_dtb_path(FLAGS_dtb);
    tmp_config_obj.set_gsi_fstab_path(FLAGS_gsi_fstab);
  } else {
    tmp_config_obj.set_system_image_path("");
    tmp_config_obj.set_vendor_image_path("");
    tmp_config_obj.set_product_image_path("");
    tmp_config_obj.set_super_image_path(FLAGS_super_image);
    tmp_config_obj.set_dtb_path("");
    tmp_config_obj.set_gsi_fstab_path("");
  }

  tmp_config_obj.set_ramdisk_image_path(ramdisk_path);
  tmp_config_obj.set_cache_image_path(FLAGS_cache_image);
  tmp_config_obj.set_data_image_path(FLAGS_data_image);
  tmp_config_obj.set_metadata_image_path(FLAGS_metadata_image);

  tmp_config_obj.set_mempath(FLAGS_mempath);
  tmp_config_obj.set_ivshmem_qemu_socket_path(
      tmp_config_obj.PerInstancePath("ivshmem_socket_qemu"));
  tmp_config_obj.set_ivshmem_client_socket_path(
      tmp_config_obj.PerInstancePath("ivshmem_socket_client"));
  tmp_config_obj.set_ivshmem_vector_count(memory_layout.GetRegions().size());

  if (AdbUsbEnabled(tmp_config_obj)) {
    tmp_config_obj.set_usb_v1_socket_name(tmp_config_obj.PerInstancePath("usb-v1"));
    tmp_config_obj.set_vhci_port(FLAGS_vhci_port);
    tmp_config_obj.set_usb_ip_socket_name(tmp_config_obj.PerInstancePath("usb-ip"));
  }

  tmp_config_obj.set_kernel_log_socket_name(tmp_config_obj.PerInstancePath("kernel-log"));
  tmp_config_obj.set_deprecated_boot_completed(FLAGS_deprecated_boot_completed);
  tmp_config_obj.set_console_path(tmp_config_obj.PerInstancePath("console"));
  tmp_config_obj.set_logcat_path(tmp_config_obj.PerInstancePath("logcat"));
  tmp_config_obj.set_logcat_receiver_binary(FLAGS_logcat_receiver_binary);
  tmp_config_obj.set_config_server_binary(FLAGS_config_server_binary);
  tmp_config_obj.set_launcher_log_path(tmp_config_obj.PerInstancePath("launcher.log"));
  tmp_config_obj.set_launcher_monitor_socket_path(
      tmp_config_obj.PerInstancePath("launcher_monitor.sock"));

  tmp_config_obj.set_mobile_bridge_name(FLAGS_mobile_interface);
  tmp_config_obj.set_mobile_tap_name(FLAGS_mobile_tap_name);

  tmp_config_obj.set_wifi_tap_name(FLAGS_wifi_tap_name);

  tmp_config_obj.set_wifi_guest_mac_addr(FLAGS_guest_mac_address);
  tmp_config_obj.set_wifi_host_mac_addr(FLAGS_host_mac_address);

  tmp_config_obj.set_vsock_guest_cid(FLAGS_vsock_guest_cid);

  tmp_config_obj.set_entropy_source("/dev/urandom");
  tmp_config_obj.set_uuid(FLAGS_uuid);

  tmp_config_obj.set_qemu_binary(FLAGS_qemu_binary);
  tmp_config_obj.set_crosvm_binary(FLAGS_crosvm_binary);
  tmp_config_obj.set_ivserver_binary(FLAGS_ivserver_binary);
  tmp_config_obj.set_kernel_log_monitor_binary(FLAGS_kernel_log_monitor_binary);

  tmp_config_obj.set_enable_vnc_server(FLAGS_start_vnc_server);
  tmp_config_obj.set_vnc_server_binary(FLAGS_vnc_server_binary);
  tmp_config_obj.set_vnc_server_port(FLAGS_vnc_server_port);

  tmp_config_obj.set_enable_stream_audio(FLAGS_start_stream_audio);
  tmp_config_obj.set_stream_audio_binary(FLAGS_stream_audio_binary);
  tmp_config_obj.set_stream_audio_port(FLAGS_stream_audio_port);

  tmp_config_obj.set_restart_subprocesses(FLAGS_restart_subprocesses);
  tmp_config_obj.set_run_adb_connector(FLAGS_run_adb_connector);
  tmp_config_obj.set_adb_connector_binary(FLAGS_adb_connector_binary);
  tmp_config_obj.set_virtual_usb_manager_binary(
      FLAGS_virtual_usb_manager_binary);
  tmp_config_obj.set_socket_forward_proxy_binary(
      FLAGS_socket_forward_proxy_binary);
  tmp_config_obj.set_socket_vsock_proxy_binary(FLAGS_socket_vsock_proxy_binary);
  tmp_config_obj.set_run_as_daemon(FLAGS_daemon);
  tmp_config_obj.set_run_e2e_test(FLAGS_run_e2e_test);
  tmp_config_obj.set_e2e_test_binary(FLAGS_e2e_test_binary);

  tmp_config_obj.set_data_policy(FLAGS_data_policy);
  tmp_config_obj.set_blank_data_image_mb(FLAGS_blank_data_image_mb);
  tmp_config_obj.set_blank_data_image_fmt(FLAGS_blank_data_image_fmt);

  if(!AdbUsbEnabled(tmp_config_obj)) {
    tmp_config_obj.disable_usb_adb();
  }

  tmp_config_obj.set_logcat_mode(FLAGS_logcat_mode);
  tmp_config_obj.set_logcat_vsock_port(FLAGS_logcat_vsock_port);
  tmp_config_obj.set_config_server_port(FLAGS_config_server_port);
  tmp_config_obj.set_frames_vsock_port(FLAGS_frames_vsock_port);
  if (!tmp_config_obj.enable_ivserver() && tmp_config_obj.enable_vnc_server()) {
    tmp_config_obj.add_kernel_cmdline(concat("androidboot.vsock_frames_port=",
                                             FLAGS_frames_vsock_port));
  }

  tmp_config_obj.set_enable_tombstone_receiver(FLAGS_enable_tombstone_receiver);
  tmp_config_obj.set_tombstone_receiver_port(FLAGS_tombstone_receiver_port);
  tmp_config_obj.set_tombstone_receiver_binary(FLAGS_tombstone_receiver_binary);
  if (FLAGS_enable_tombstone_receiver) {
    tmp_config_obj.add_kernel_cmdline("androidboot.tombstone_transmit=1");
    tmp_config_obj.add_kernel_cmdline(concat("androidboot.vsock_tombstone_port="
      ,FLAGS_tombstone_receiver_port));
    // TODO (b/128842613) populate a cid flag to read the host CID during
    // runtime
  } else {
    tmp_config_obj.add_kernel_cmdline("androidboot.tombstone_transmit=0");
  }

  tmp_config_obj.set_cuttlefish_env_path(GetCuttlefishEnvPath());

  auto config_file = GetConfigFilePath(tmp_config_obj);
  auto config_link = vsoc::GetGlobalConfigFileLink();
  // Save the config object before starting any host process
  if (!tmp_config_obj.SaveToFile(config_file)) {
    LOG(ERROR) << "Unable to save config object";
    return false;
  }
  setenv(vsoc::kCuttlefishConfigEnvVarName, config_file.c_str(), true);
  if (symlink(config_file.c_str(), config_link.c_str()) != 0) {
    LOG(ERROR) << "Failed to create symlink to config file at " << config_link
               << ": " << strerror(errno);
    return false;
  }

  return true;
}

void SetDefaultFlagsForQemu() {
  auto default_mobile_interface = GetPerInstanceDefault("cvd-mbr-");
  SetCommandLineOptionWithMode("mobile_interface",
                               default_mobile_interface.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  auto default_mobile_tap_name = GetPerInstanceDefault("cvd-mtap-");
  SetCommandLineOptionWithMode("mobile_tap_name",
                               default_mobile_tap_name.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  auto default_wifi_tap_name = GetPerInstanceDefault("cvd-wtap-");
  SetCommandLineOptionWithMode("wifi_tap_name",
                               default_wifi_tap_name.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  auto default_instance_dir =
      cvd::StringFromEnv("HOME", ".") + "/cuttlefish_runtime";
  SetCommandLineOptionWithMode("instance_dir",
                               default_instance_dir.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("hardware_name", "cutf_ivsh",
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("decompress_kernel", "false",
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("logcat_mode", cvd::kLogcatSerialMode,
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
}

void SetDefaultFlagsForCrosvm() {
  auto default_mobile_interface = GetPerInstanceDefault("cvd-mbr-");
  SetCommandLineOptionWithMode("mobile_interface",
                               default_mobile_interface.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  auto default_mobile_tap_name = GetPerInstanceDefault("cvd-mtap-");
  SetCommandLineOptionWithMode("mobile_tap_name",
                               default_mobile_tap_name.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  auto default_wifi_tap_name = GetPerInstanceDefault("cvd-wtap-");
  SetCommandLineOptionWithMode("wifi_tap_name",
                               default_wifi_tap_name.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  auto default_instance_dir =
      cvd::StringFromEnv("HOME", ".") + "/cuttlefish_runtime";
  SetCommandLineOptionWithMode("instance_dir",
                               default_instance_dir.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  auto default_wayland_socket = vsoc::DefaultEnvironmentPath(
      "XDG_RUNTIME_DIR", default_instance_dir.c_str(), "wayland-0");
  SetCommandLineOptionWithMode("wayland_socket",
                               default_wayland_socket.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("hardware_name", "cutf_cvm",
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("decompress_kernel", "true",
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("run_e2e_test", "false",
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  SetCommandLineOptionWithMode("logcat_mode", cvd::kLogcatVsockMode,
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
}

bool ParseCommandLineFlags(int* argc, char*** argv) {
  // The config_file is created by the launcher, so the launcher is the only
  // host process that doesn't use the flag.
  // Set the default to empty.
  google::SetCommandLineOptionWithMode("config_file", "",
                                       gflags::SET_FLAGS_DEFAULT);
  google::ParseCommandLineNonHelpFlags(argc, argv, true);
  bool invalid_manager = false;
  if (FLAGS_vm_manager == vm_manager::QemuManager::name()) {
    SetDefaultFlagsForQemu();
  } else if (FLAGS_vm_manager == vm_manager::CrosvmManager::name()) {
    SetDefaultFlagsForCrosvm();
  } else {
    std::cerr << "Unknown Virtual Machine Manager: " << FLAGS_vm_manager
              << std::endl;
    invalid_manager = true;
  }
  google::HandleCommandLineHelpFlags();
  if (invalid_manager) {
    return false;
  }
  // Set the env variable to empty (in case the caller passed a value for it).
  unsetenv(vsoc::kCuttlefishConfigEnvVarName);

  return ResolveInstanceFiles();
}

bool CleanPriorFiles() {
  // Everything on the instance directory
  std::string prior_files = FLAGS_instance_dir + "/*";
  // The shared memory file
  prior_files += " " + FLAGS_mempath;
  // The environment file
  prior_files += " " + GetCuttlefishEnvPath();
  // The global link to the config file
  prior_files += " " + vsoc::GetGlobalConfigFileLink();
  LOG(INFO) << "Assuming prior files of " << prior_files;
  std::string fuser_cmd = "fuser " + prior_files + " 2> /dev/null";
  int rval = std::system(fuser_cmd.c_str());
  // fuser returns 0 if any of the files are open
  if (WEXITSTATUS(rval) == 0) {
    LOG(ERROR) << "Clean aborted: files are in use";
    return false;
  }
  std::string clean_command = "rm -rf " + prior_files;
  rval = std::system(clean_command.c_str());
  if (WEXITSTATUS(rval) != 0) {
    LOG(ERROR) << "Remove of files failed";
    return false;
  }
  return true;
}

bool DecompressKernel(const std::string& src, const std::string& dst) {
  cvd::Command decomp_cmd(FLAGS_kernel_decompresser_executable);
  decomp_cmd.AddParameter(src);
  auto output_file = cvd::SharedFD::Creat(dst.c_str(), 0666);
  if (!output_file->IsOpen()) {
    LOG(ERROR) << "Unable to create decompressed image file: "
               << output_file->StrError();
    return false;
  }
  decomp_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdOut, output_file);
  auto decomp_proc = decomp_cmd.Start(false);
  return decomp_proc.Started() && decomp_proc.Wait() == 0;
}
} // namespace

vsoc::CuttlefishConfig* InitFilesystemAndCreateConfig(int* argc, char*** argv) {
  if (!ParseCommandLineFlags(argc, argv)) {
    LOG(ERROR) << "Failed to parse command arguments";
    exit(LauncherExitCodes::kArgumentParsingError);
  }

  // Clean up prior files before saving the config file (doing it after would
  // delete it)
  if (!CleanPriorFiles()) {
    LOG(ERROR) << "Failed to clean prior files";
    exit(LauncherExitCodes::kPrioFilesCleanupError);
  }
  // Create instance directory if it doesn't exist.
  if (!cvd::DirectoryExists(FLAGS_instance_dir.c_str())) {
    LOG(INFO) << "Setting up " << FLAGS_instance_dir;
    if (mkdir(FLAGS_instance_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0) {
      LOG(ERROR) << "Failed to create instance directory: "
                 << FLAGS_instance_dir << ". Error: " << errno;
      exit(LauncherExitCodes::kInstanceDirCreationError);
    }
  }

  if (!cvd::FileHasContent(FLAGS_boot_image)) {
    LOG(ERROR) << "File not found: " << FLAGS_boot_image;
    exit(cvd::kCuttlefishConfigurationInitError);
  }

  auto boot_img_unpacker = cvd::BootImageUnpacker::FromImage(FLAGS_boot_image);

  if (!InitializeCuttlefishConfiguration(*boot_img_unpacker)) {
    LOG(ERROR) << "Failed to initialize configuration";
    exit(LauncherExitCodes::kCuttlefishConfigurationInitError);
  }
  // Do this early so that the config object is ready for anything that needs it
  auto config = vsoc::CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Failed to obtain config singleton";
    exit(LauncherExitCodes::kCuttlefishConfigurationInitError);
  }

  if (!boot_img_unpacker->Unpack(config->ramdisk_image_path(),
                                 config->use_unpacked_kernel()
                                     ? config->kernel_image_path()
                                     : "")) {
    LOG(ERROR) << "Failed to unpack boot image";
    exit(LauncherExitCodes::kBootImageUnpackError);
  }

  if (config->decompress_kernel()) {
    if (!DecompressKernel(config->kernel_image_path(),
        config->decompressed_kernel_image_path())) {
      LOG(ERROR) << "Failed to decompress kernel";
      exit(LauncherExitCodes::kKernelDecompressError);
    }
  }

  ValidateAdbModeFlag(*config);

  // Create data if necessary
  if (!ApplyDataImagePolicy(*config)) {
    exit(cvd::kCuttlefishConfigurationInitError);
  }

  CreateBlankImage(FLAGS_metadata_image, FLAGS_blank_metadata_image_mb, "none");

  // Check that the files exist
  for (const auto& file :
       {config->system_image_path(), config->cache_image_path(),
        config->data_image_path(), config->vendor_image_path(),
        config->metadata_image_path(),  config->product_image_path(),
        config->super_image_path()}) {
    if (!file.empty() && !cvd::FileHasContent(file.c_str())) {
      LOG(ERROR) << "File not found: " << file;
      exit(cvd::kCuttlefishConfigurationInitError);
    }
  }

  return config;
}

std::string GetConfigFilePath(const vsoc::CuttlefishConfig& config) {
  return config.PerInstancePath("cuttlefish_config.json");
}
