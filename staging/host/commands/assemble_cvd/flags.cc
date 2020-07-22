#include "host/commands/assemble_cvd/flags.h"

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <fcntl.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <fstream>
#include <set>

#include <android-base/strings.h>
#include <gflags/gflags.h>
#include <android-base/logging.h>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/assemble_cvd/boot_config.h"
#include "host/commands/assemble_cvd/boot_image_unpacker.h"
#include "host/commands/assemble_cvd/image_aggregator.h"
#include "host/commands/assemble_cvd/assembler_defs.h"
#include "host/commands/assemble_cvd/super_image_mixer.h"
#include "host/libs/config/data_image.h"
#include "host/libs/config/fetcher_config.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"
#include "host/libs/vm_manager/vm_manager.h"

// Taken from external/avb/libavb/avb_slot_verify.c; this define is not in the headers
#define VBMETA_MAX_SIZE 65536ul

using cuttlefish::CreateBlankImage;
using cuttlefish::DataImageResult;
using cuttlefish::ForCurrentInstance;
using cuttlefish::InitializeMiscImage;
using cuttlefish::RandomSerialNumber;
using cuttlefish::AssemblerExitCodes;
using cuttlefish::vm_manager::CrosvmManager;
using cuttlefish::vm_manager::QemuManager;
using cuttlefish::vm_manager::VmManager;

DEFINE_string(cache_image, "", "Location of the cache partition image.");
DEFINE_string(metadata_image, "", "Location of the metadata partition image "
              "to be generated.");
DEFINE_int32(blank_metadata_image_mb, 16,
             "The size of the blank metadata image to generate, MB.");
DEFINE_int32(blank_sdcard_image_mb, 2048,
             "The size of the blank sdcard image to generate, MB.");
DEFINE_int32(cpus, 2, "Virtual CPU count.");
DEFINE_string(data_image, "", "Location of the data partition image.");
DEFINE_string(data_policy, "use_existing", "How to handle userdata partition."
            " Either 'use_existing', 'create_if_missing', 'resize_up_to', or "
            "'always_create'.");
DEFINE_int32(blank_data_image_mb, 0,
             "The size of the blank data image to generate, MB.");
DEFINE_string(blank_data_image_fmt, "f2fs",
              "The fs format for the blank data image. Used with mkfs.");
DEFINE_string(qemu_gdb, "",
              "Debug flag to pass to qemu. e.g. -qemu_gdb=tcp::1234");

DEFINE_int32(x_res, 720, "Width of the screen in pixels");
DEFINE_int32(y_res, 1280, "Height of the screen in pixels");
DEFINE_int32(dpi, 160, "Pixels per inch for the screen");
DEFINE_int32(refresh_rate_hz, 60, "Screen refresh rate in Hertz");
DEFINE_string(kernel_path, "",
              "Path to the kernel. Overrides the one from the boot image");
DEFINE_string(initramfs_path, "", "Path to the initramfs");
DEFINE_bool(decompress_kernel, false,
            "Whether to decompress the kernel image.");
DEFINE_string(extra_kernel_cmdline, "",
              "Additional flags to put on the kernel command line");
DEFINE_int32(loop_max_part, 7, "Maximum number of loop partitions");
DEFINE_bool(guest_enforce_security, true,
            "Whether to run in enforcing mode (non permissive).");
DEFINE_bool(guest_audit_security, true,
            "Whether to log security audits.");
DEFINE_bool(guest_force_normal_boot, true,
            "Whether to force the boot sequence to skip recovery.");
DEFINE_string(boot_image, "",
              "Location of cuttlefish boot image. If empty it is assumed to be "
              "boot.img in the directory specified by -system_image_dir.");
DEFINE_string(vendor_boot_image, "",
              "Location of cuttlefish vendor boot image. If empty it is assumed to "
              "be vendor_boot.img in the directory specified by -system_image_dir.");
DEFINE_string(vbmeta_image, "",
              "Location of cuttlefish vbmeta image. If empty it is assumed to "
              "be vbmeta.img in the directory specified by -system_image_dir.");
DEFINE_string(vbmeta_system_image, "",
              "Location of cuttlefish vbmeta_system image. If empty it is assumed to "
              "be vbmeta_system.img in the directory specified by -system_image_dir.");
DEFINE_int32(memory_mb, 2048,
             "Total amount of memory available for guest, MB.");
DEFINE_string(serial_number, ForCurrentInstance("CUTTLEFISHCVD"),
              "Serial number to use for the device");
DEFINE_bool(use_random_serial, false,
            "Whether to use random serial for the device.");
DEFINE_string(assembly_dir,
              cuttlefish::DefaultHostArtifactsPath("cuttlefish_assembly"),
              "A directory to put generated files common between instances");
DEFINE_string(instance_dir,
              cuttlefish::StringFromEnv("HOME", ".") + "/cuttlefish_runtime",
              "A directory to put all instance specific files");
DEFINE_string(
    vm_manager, CrosvmManager::name(),
    "What virtual machine manager to use, one of {qemu_cli, crosvm}");
DEFINE_string(
    gpu_mode, cuttlefish::kGpuModeGuestSwiftshader,
    "What gpu configuration to use, one of {guest_swiftshader, drm_virgl}");

DEFINE_string(system_image_dir, cuttlefish::DefaultGuestImagePath(""),
              "Location of the system partition images.");
DEFINE_string(super_image, "", "Location of the super partition image.");
DEFINE_string(misc_image, "",
              "Location of the misc partition image. If the image does not "
              "exist, a blank new misc partition image is created.");
DEFINE_string(boot_env_image, "",
              "Location of the boot environment image. If the image does not "
              "exist, a default boot environment image is created.");

DEFINE_bool(deprecated_boot_completed, false, "Log boot completed message to"
            " host kernel. This is only used during transition of our clients."
            " Will be deprecated soon.");
DEFINE_bool(start_vnc_server, false, "Whether to start the vnc server process. "
                                     "The VNC server runs at port 6443 + i for "
                                     "the vsoc-i user or CUTTLEFISH_INSTANCE=i, "
                                     "starting from 1.");
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
    std::string("usr/share/cuttlefish/") + cuttlefish::HostArch() + "-linux-gnu/seccomp";
DEFINE_string(seccomp_policy_dir,
              cuttlefish::DefaultHostArtifactsPath(kSeccompDir),
              "With sandbox'ed crosvm, overrieds the security comp policy directory");

DEFINE_bool(start_webrtc, false, "Whether to start the webrtc process.");

DEFINE_string(
        webrtc_assets_dir,
        cuttlefish::DefaultHostArtifactsPath("usr/share/webrtc/assets"),
        "[Experimental] Path to WebRTC webpage assets.");

DEFINE_string(
        webrtc_certs_dir,
        cuttlefish::DefaultHostArtifactsPath("usr/share/webrtc/certs"),
        "[Experimental] Path to WebRTC certificates directory.");

DEFINE_string(
        webrtc_public_ip,
        "127.0.0.1",
        "[Experimental] Public IPv4 address of your server, a.b.c.d format");

DEFINE_bool(
        webrtc_enable_adb_websocket,
        false,
        "[Experimental] If enabled, exposes local adb service through a websocket.");

DEFINE_bool(
    start_webrtc_sig_server, false,
    "Whether to start the webrtc signaling server. This option only applies to "
    "the first instance, if multiple instances are launched they'll share the "
    "same signaling server, which is owned by the first one.");

DEFINE_string(webrtc_sig_server_addr, "127.0.0.1",
              "The address of the webrtc signaling server.");

DEFINE_int32(
    webrtc_sig_server_port, 443,
    "The port of the signaling server if started outside of this launch. If "
    "-start_webrtc_sig_server is given it will choose 8443+instance_num1-1 and "
    "this parameter is ignored.");

DEFINE_string(webrtc_sig_server_path, "/register_device",
              "The path section of the URL where the device should be "
              "registered with the signaling server.");

DEFINE_bool(verify_sig_server_certificate, false,
            "Whether to verify the signaling server's certificate with a "
            "trusted signing authority (Disallow self signed certificates).");

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
DEFINE_bool(run_adb_connector, true,
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
DEFINE_string(crosvm_binary,
              cuttlefish::DefaultHostArtifactsPath("bin/crosvm"),
              "The Crosvm binary to use");
DEFINE_string(tpm_binary, "",
              "The TPM simulator to use. Disabled if empty.");
DEFINE_string(tpm_device, "", "A host TPM device to pass through commands to.");
DEFINE_bool(restart_subprocesses, true, "Restart any crashed host process");
DEFINE_bool(enable_tombstone_receiver, true, "Enables the tombstone logger on "
            "both the guest and the host");
DEFINE_bool(enable_vehicle_hal_grpc_server, true, "Enables the vehicle HAL "
            "emulation gRPC server on the host");
DEFINE_bool(use_bootloader, false, "Boots the device using a bootloader");
DEFINE_string(bootloader, "", "Bootloader binary path");
DEFINE_string(boot_slot, "", "Force booting into the given slot. If empty, "
             "the slot will be chosen based on the misc partition if using a "
             "bootloader. It will default to 'a' if empty and not using a "
             "bootloader.");
DEFINE_int32(num_instances, 1, "Number of Android guests to launch");
DEFINE_bool(resume, true, "Resume using the disk from the last session, if "
                          "possible. i.e., if --noresume is passed, the disk "
                          "will be reset to the state it was initially launched "
                          "in. This flag is ignored if the underlying partition "
                          "images have been updated since the first launch.");
DEFINE_string(report_anonymous_usage_stats, "", "Report anonymous usage "
            "statistics for metrics collection and analysis.");
DEFINE_string(ril_dns, "8.8.8.8", "DNS address of mobile network (RIL)");
DEFINE_bool(kgdb, false, "Configure the virtual device for debugging the kernel "
                         "with kgdb/kdb. The kernel must have been built with "
                         "kgdb support.");

namespace {

const std::string kKernelDefaultPath = "kernel";
const std::string kInitramfsImg = "initramfs.img";
const std::string kRamdiskConcatExt = ".concat";

bool ResolveInstanceFiles() {
  if (FLAGS_system_image_dir.empty()) {
    LOG(ERROR) << "--system_image_dir must be specified.";
    return false;
  }

  // If user did not specify location of either of these files, expect them to
  // be placed in --system_image_dir location.
  std::string default_boot_image = FLAGS_system_image_dir + "/boot.img";
  SetCommandLineOptionWithMode("boot_image", default_boot_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_cache_image = FLAGS_system_image_dir + "/cache.img";
  SetCommandLineOptionWithMode("cache_image", default_cache_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_data_image = FLAGS_system_image_dir + "/userdata.img";
  SetCommandLineOptionWithMode("data_image", default_data_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_metadata_image = FLAGS_system_image_dir + "/metadata.img";
  SetCommandLineOptionWithMode("metadata_image", default_metadata_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_super_image = FLAGS_system_image_dir + "/super.img";
  SetCommandLineOptionWithMode("super_image", default_super_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_misc_image = FLAGS_system_image_dir + "/misc.img";
  SetCommandLineOptionWithMode("misc_image", default_misc_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_vendor_boot_image = FLAGS_system_image_dir
                                        + "/vendor_boot.img";
  SetCommandLineOptionWithMode("vendor_boot_image",
                               default_vendor_boot_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_boot_env_image = FLAGS_system_image_dir + "/env.img";
  SetCommandLineOptionWithMode("boot_env_image", default_boot_env_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_vbmeta_image = FLAGS_system_image_dir + "/vbmeta.img";
  SetCommandLineOptionWithMode("vbmeta_image", default_vbmeta_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  std::string default_vbmeta_system_image = FLAGS_system_image_dir
                                          + "/vbmeta_system.img";
  SetCommandLineOptionWithMode("vbmeta_system_image",
                               default_vbmeta_system_image.c_str(),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);

  return true;
}

std::string GetCuttlefishEnvPath() {
  return cuttlefish::StringFromEnv("HOME", ".") + "/.cuttlefish.sh";
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

// Initializes the config object and saves it to file. It doesn't return it, all
// further uses of the config should happen through the singleton
cuttlefish::CuttlefishConfig InitializeCuttlefishConfiguration(
    const cuttlefish::BootImageUnpacker& boot_image_unpacker,
    const cuttlefish::FetcherConfig& fetcher_config) {
  // At most one streamer can be started.
  CHECK(NumStreamers() <= 1);

  cuttlefish::CuttlefishConfig tmp_config_obj;
  tmp_config_obj.set_assembly_dir(FLAGS_assembly_dir);
  if (!VmManager::IsValidName(FLAGS_vm_manager)) {
    LOG(FATAL) << "Invalid vm_manager: " << FLAGS_vm_manager;
  }
  if (!VmManager::IsValidName(FLAGS_vm_manager)) {
    LOG(FATAL) << "Invalid vm_manager: " << FLAGS_vm_manager;
  }
  tmp_config_obj.set_vm_manager(FLAGS_vm_manager);
  tmp_config_obj.set_gpu_mode(FLAGS_gpu_mode);
  if (VmManager::ConfigureGpuMode(tmp_config_obj.vm_manager(),
                                  tmp_config_obj.gpu_mode()).empty()) {
    LOG(FATAL) << "Invalid gpu_mode=" << FLAGS_gpu_mode <<
               " does not work with vm_manager=" << FLAGS_vm_manager;
  }

  tmp_config_obj.set_cpus(FLAGS_cpus);
  tmp_config_obj.set_memory_mb(FLAGS_memory_mb);

  tmp_config_obj.set_dpi(FLAGS_dpi);
  tmp_config_obj.set_setupwizard_mode(FLAGS_setupwizard_mode);
  tmp_config_obj.set_x_res(FLAGS_x_res);
  tmp_config_obj.set_y_res(FLAGS_y_res);
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

  tmp_config_obj.set_boot_image_kernel_cmdline(boot_image_unpacker.kernel_cmdline());
  tmp_config_obj.set_loop_max_part(FLAGS_loop_max_part);
  tmp_config_obj.set_guest_enforce_security(FLAGS_guest_enforce_security);
  tmp_config_obj.set_guest_audit_security(FLAGS_guest_audit_security);
  tmp_config_obj.set_guest_force_normal_boot(FLAGS_guest_force_normal_boot);
  tmp_config_obj.set_extra_kernel_cmdline(FLAGS_extra_kernel_cmdline);

  std::string vm_manager_cmdline = "";
  if (FLAGS_vm_manager == QemuManager::name() || FLAGS_use_bootloader) {
    // crosvm sets up the console= earlycon= pci= reboot= panic= flags for us if
    // booting straight to the kernel, but QEMU and the bootlaoder via crosvm does not.
    vm_manager_cmdline += "console=hvc0 pci=noacpi reboot=k panic=-1";
    if (cuttlefish::HostArch() == "aarch64") {
      if (FLAGS_vm_manager == QemuManager::name()) {
        // To update the pl011 address:
        // $ qemu-system-aarch64 -machine virt -cpu cortex-a57 -machine dumpdtb=virt.dtb
        // $ dtc -O dts -o virt.dts -I dtb virt.dtb
        // In the virt.dts file, look for a uart node
        vm_manager_cmdline += " earlycon=pl011,mmio32,0x9000000";
      } else {
        // Crosvm ARM only supports earlycon uart over mmio.
        vm_manager_cmdline += " earlycon=uart8250,mmio,0x3f8";
      }
    } else {
      // To update the uart8250 address:
      // $ qemu-system-x86_64 -kernel bzImage -serial stdio | grep ttyS0
      // Only 'io' mode works; mmio and mmio32 do not
      vm_manager_cmdline += " earlycon=uart8250,io,0x3f8";
    }
  }

  if (FLAGS_kgdb) {
    vm_manager_cmdline += " kgdboc_earlycon kgdbcon";
    // crosvm ARM does not support ttyAMA. ttyAMA is a part of ARM arch.
    if (FLAGS_vm_manager == QemuManager::name() && cuttlefish::HostArch() == "aarch64") {
      vm_manager_cmdline += " androidboot.console=ttyAMA0 kgdboc=ttyAMA0";
    } else {
      vm_manager_cmdline += " androidboot.console=ttyS0 kgdboc=ttyS0";
    }
  } else if (FLAGS_use_bootloader) {
    // However, if the bootloader is enabled, virtio console can't
    // be used since uboot doesn't support it.
    vm_manager_cmdline += " androidboot.console=ttyS1";
  } else {
    // If kgdb is disabled, the Android serial console spawns on a
    // virtio-console port
    vm_manager_cmdline += " androidboot.console=hvc1";
  }
  tmp_config_obj.set_vm_manager_kernel_cmdline(vm_manager_cmdline);

  tmp_config_obj.set_ramdisk_image_path(ramdisk_path);
  tmp_config_obj.set_vendor_ramdisk_image_path(vendor_ramdisk_path);

  std::string discovered_ramdisk = fetcher_config.FindCvdFileWithSuffix(kInitramfsImg);
  std::string foreign_ramdisk = FLAGS_initramfs_path.size () ? FLAGS_initramfs_path : discovered_ramdisk;
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

  tmp_config_obj.set_deprecated_boot_completed(FLAGS_deprecated_boot_completed);
  tmp_config_obj.set_logcat_receiver_binary(
      cuttlefish::DefaultHostArtifactsPath("bin/logcat_receiver"));
  tmp_config_obj.set_config_server_binary(
      cuttlefish::DefaultHostArtifactsPath("bin/config_server"));

  tmp_config_obj.set_qemu_binary(FLAGS_qemu_binary);
  tmp_config_obj.set_crosvm_binary(FLAGS_crosvm_binary);
  tmp_config_obj.set_tpm_binary(FLAGS_tpm_binary);
  tmp_config_obj.set_tpm_device(FLAGS_tpm_device);
  tmp_config_obj.set_console_forwarder_binary(
      cuttlefish::DefaultHostArtifactsPath("bin/console_forwarder"));
  tmp_config_obj.set_kernel_log_monitor_binary(
      cuttlefish::DefaultHostArtifactsPath("bin/kernel_log_monitor"));

  tmp_config_obj.set_enable_vnc_server(FLAGS_start_vnc_server);
  tmp_config_obj.set_vnc_server_binary(
      cuttlefish::DefaultHostArtifactsPath("bin/vnc_server"));

  tmp_config_obj.set_enable_sandbox(FLAGS_enable_sandbox);

  tmp_config_obj.set_seccomp_policy_dir(FLAGS_seccomp_policy_dir);

  tmp_config_obj.set_enable_webrtc(FLAGS_start_webrtc);
  tmp_config_obj.set_webrtc_binary(
      cuttlefish::DefaultHostArtifactsPath("bin/webRTC"));
  tmp_config_obj.set_webrtc_assets_dir(FLAGS_webrtc_assets_dir);
  tmp_config_obj.set_webrtc_public_ip(FLAGS_webrtc_public_ip);
  tmp_config_obj.set_webrtc_certs_dir(FLAGS_webrtc_certs_dir);
  tmp_config_obj.set_sig_server_binary(
      cuttlefish::DefaultHostArtifactsPath("bin/webrtc_sig_server"));
  // Note: This will be overridden if the sig server is started by us
  tmp_config_obj.set_sig_server_port(FLAGS_webrtc_sig_server_port);
  tmp_config_obj.set_sig_server_address(FLAGS_webrtc_sig_server_addr);
  tmp_config_obj.set_sig_server_path(FLAGS_webrtc_sig_server_path);
  tmp_config_obj.set_sig_server_strict(FLAGS_verify_sig_server_certificate);

  tmp_config_obj.set_webrtc_enable_adb_websocket(
          FLAGS_webrtc_enable_adb_websocket);

  tmp_config_obj.set_restart_subprocesses(FLAGS_restart_subprocesses);
  tmp_config_obj.set_run_adb_connector(FLAGS_run_adb_connector);
  tmp_config_obj.set_adb_connector_binary(
      cuttlefish::DefaultHostArtifactsPath("bin/adb_connector"));
  tmp_config_obj.set_socket_vsock_proxy_binary(
      cuttlefish::DefaultHostArtifactsPath("bin/socket_vsock_proxy"));
  tmp_config_obj.set_run_as_daemon(FLAGS_daemon);

  tmp_config_obj.set_data_policy(FLAGS_data_policy);
  tmp_config_obj.set_blank_data_image_mb(FLAGS_blank_data_image_mb);
  tmp_config_obj.set_blank_data_image_fmt(FLAGS_blank_data_image_fmt);

  tmp_config_obj.set_enable_tombstone_receiver(FLAGS_enable_tombstone_receiver);
  tmp_config_obj.set_tombstone_receiver_binary(
      cuttlefish::DefaultHostArtifactsPath("bin/tombstone_receiver"));

  tmp_config_obj.set_enable_vehicle_hal_grpc_server(FLAGS_enable_vehicle_hal_grpc_server);
  tmp_config_obj.set_vehicle_hal_grpc_server_binary(
      cuttlefish::DefaultHostArtifactsPath("bin/android.hardware.automotive.vehicle@2.0-virtualization-grpc-server"));

  tmp_config_obj.set_use_bootloader(FLAGS_use_bootloader);
  tmp_config_obj.set_bootloader(FLAGS_bootloader);

  tmp_config_obj.set_enable_metrics(FLAGS_report_anonymous_usage_stats);
  tmp_config_obj.set_metrics_binary(
      cuttlefish::DefaultHostArtifactsPath("bin/metrics"));

  if (!FLAGS_boot_slot.empty()) {
      tmp_config_obj.set_boot_slot(FLAGS_boot_slot);
  }

  tmp_config_obj.set_cuttlefish_env_path(GetCuttlefishEnvPath());

  tmp_config_obj.set_ril_dns(FLAGS_ril_dns);

  tmp_config_obj.set_kgdb(FLAGS_kgdb);

  std::vector<int> instance_nums;
  for (int i = 0; i < FLAGS_num_instances; i++) {
    instance_nums.push_back(cuttlefish::GetInstance() + i);
  }

  bool is_first_instance = true;
  for (const auto& num : instance_nums) {
    auto instance = tmp_config_obj.ForInstance(num);
    auto const_instance = const_cast<const cuttlefish::CuttlefishConfig&>(tmp_config_obj)
        .ForInstance(num);
    // Set this first so that calls to PerInstancePath below are correct
    instance.set_instance_dir(FLAGS_instance_dir + "." + std::to_string(num));
    if(FLAGS_use_random_serial){
      instance.set_serial_number(RandomSerialNumber("CFCVD" + std::to_string(num)));
    } else {
      instance.set_serial_number(FLAGS_serial_number + std::to_string(num));
    }

    instance.set_mobile_bridge_name(StrForInstance("cvd-mbr-", num));
    instance.set_mobile_tap_name(StrForInstance("cvd-mtap-", num));

    instance.set_wifi_tap_name(StrForInstance("cvd-wtap-", num));

    instance.set_vsock_guest_cid(3 + num - 1);

    instance.set_uuid(FLAGS_uuid);

    instance.set_vnc_server_port(6444 + num - 1);
    instance.set_host_port(6520 + num - 1);
    instance.set_adb_ip_and_port("127.0.0.1:" + std::to_string(6520 + num - 1));
    instance.set_tpm_port(2321 + (num * 2) - 2);
    instance.set_tombstone_receiver_port(6600 + num - 1);
    instance.set_vehicle_hal_server_port(9210 + num - 1);
    instance.set_config_server_port(6800 + num - 1);

    if (FLAGS_gpu_mode != cuttlefish::kGpuModeDrmVirgl &&
        FLAGS_gpu_mode != cuttlefish::kGpuModeGfxStream) {
      instance.set_frames_server_port(6900 + num - 1);
    }

    if (FLAGS_vm_manager == QemuManager::name()) {
      instance.set_keyboard_server_port(7000 + num - 1);
      instance.set_touch_server_port(7100 + num - 1);
    }
    instance.set_keymaster_vsock_port(7200 + num - 1);
    instance.set_gatekeeper_vsock_port(7300 + num - 1);

    instance.set_device_title(FLAGS_device_title);

    instance.set_virtual_disk_paths({
      const_instance.PerInstancePath("overlay.img"),
      const_instance.sdcard_path(),
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
  }

  return tmp_config_obj;
}

bool SaveConfig(const cuttlefish::CuttlefishConfig& tmp_config_obj) {
  auto config_file = GetConfigFilePath(tmp_config_obj);
  auto config_link = cuttlefish::GetGlobalConfigFileLink();
  // Save the config object before starting any host process
  if (!tmp_config_obj.SaveToFile(config_file)) {
    LOG(ERROR) << "Unable to save config object";
    return false;
  }
  for (auto instance : tmp_config_obj.Instances()) {
    if (!tmp_config_obj.SaveToFile(
        instance.PerInstancePath("cuttlefish_config.json"))) {
      LOG(ERROR) << "Unable to save copy config object";
      return false;
    }
  }
  setenv(cuttlefish::kCuttlefishConfigEnvVarName, config_file.c_str(), true);
  if (symlink(config_file.c_str(), config_link.c_str()) != 0) {
    LOG(ERROR) << "Failed to create symlink to config file at " << config_link
               << ": " << strerror(errno);
    return false;
  }

  return true;
}

void SetDefaultFlagsForQemu() {
  // for now, we don't set non-default options for QEMU
}

void SetDefaultFlagsForCrosvm() {
  // for now, we support only x86_64 by default
  bool default_enable_sandbox = false;
  std::set<const std::string> supported_archs{std::string("x86_64")};
  if (supported_archs.find(cuttlefish::HostArch()) != supported_archs.end()) {
    default_enable_sandbox =
        [](const std::string& var_empty) -> bool {
          if (cuttlefish::DirectoryExists(var_empty)) {
            return cuttlefish::IsDirectoryEmpty(var_empty);
          }
          if (cuttlefish::FileExists(var_empty)) {
            return false;
          }
          return (::mkdir(var_empty.c_str(), 0755) == 0);
        }(cuttlefish::kCrosvmVarEmptyDir);
  }

  // Sepolicy rules need to be updated to support gpu mode. Temporarily disable
  // auto-enabling sandbox when gpu is enabled (b/152323505).
  if (FLAGS_gpu_mode != cuttlefish::kGpuModeGuestSwiftshader) {
    default_enable_sandbox = false;
  }

  SetCommandLineOptionWithMode("enable_sandbox",
                               (default_enable_sandbox ? "true" : "false"),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);

  // Crosvm requires a specific setting for kernel decompression; it must be
  // on for aarch64 and off for x86, no other mode is supported.
  bool decompress_kernel = false;
  if (cuttlefish::HostArch() == "aarch64") {
    decompress_kernel = true;
  }
  SetCommandLineOptionWithMode("decompress_kernel",
                               (decompress_kernel ? "true" : "false"),
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
}

bool ParseCommandLineFlags(int* argc, char*** argv) {
  google::ParseCommandLineNonHelpFlags(argc, argv, true);
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
  if (NumStreamers() == 0) {
    // This makes the vnc server the default streamer unless the user requests
    // another via a --star_<streamer> flag, while at the same time it's
    // possible to run without any streamer by setting --start_vnc_server=false.
    SetCommandLineOptionWithMode("start_vnc_server", "true",
                                 google::FlagSettingMode::SET_FLAGS_DEFAULT);
  }
  // Various temporary workarounds for aarch64
  if (cuttlefish::HostArch() == "aarch64") {
    SetCommandLineOptionWithMode("tpm_binary",
                                 "",
                                 google::FlagSettingMode::SET_FLAGS_DEFAULT);
  }
  // The default for starting signaling server is whether or not webrt is to be
  // started.
  SetCommandLineOptionWithMode("start_webrtc_sig_server",
                               FLAGS_start_webrtc ? "true" : "false",
                               google::FlagSettingMode::SET_FLAGS_DEFAULT);
  google::HandleCommandLineHelpFlags();
  if (invalid_manager) {
    return false;
  }
  // Set the env variable to empty (in case the caller passed a value for it).
  unsetenv(cuttlefish::kCuttlefishConfigEnvVarName);

  return ResolveInstanceFiles();
}

std::string cpp_basename(const std::string& str) {
  char* copy = strdup(str.c_str()); // basename may modify its argument
  std::string ret(basename(copy));
  free(copy);
  return ret;
}

bool CleanPriorFiles(const std::string& path, const std::set<std::string>& preserving) {
  if (preserving.count(cpp_basename(path))) {
    LOG(DEBUG) << "Preserving: " << path;
    return true;
  }
  struct stat statbuf;
  if (lstat(path.c_str(), &statbuf) < 0) {
    int error_num = errno;
    if (error_num == ENOENT) {
      return true;
    } else {
      LOG(ERROR) << "Could not stat \"" << path << "\": " << strerror(error_num);
      return false;
    }
  }
  if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
    LOG(DEBUG) << "Deleting: " << path;
    if (unlink(path.c_str()) < 0) {
      int error_num = errno;
      LOG(ERROR) << "Could not unlink \"" << path << "\", error was " << strerror(error_num);
      return false;
    }
    return true;
  }
  std::unique_ptr<DIR, int(*)(DIR*)> dir(opendir(path.c_str()), closedir);
  if (!dir) {
    int error_num = errno;
    LOG(ERROR) << "Could not clean \"" << path << "\": error was " << strerror(error_num);
    return false;
  }
  for (auto entity = readdir(dir.get()); entity != nullptr; entity = readdir(dir.get())) {
    std::string entity_name(entity->d_name);
    if (entity_name == "." || entity_name == "..") {
      continue;
    }
    std::string entity_path = path + "/" + entity_name;
    if (!CleanPriorFiles(entity_path.c_str(), preserving)) {
      return false;
    }
  }
  if (rmdir(path.c_str()) < 0) {
    if (!(errno == EEXIST || errno == ENOTEMPTY)) {
      // If EEXIST or ENOTEMPTY, probably because a file was preserved
      int error_num = errno;
      LOG(ERROR) << "Could not rmdir \"" << path << "\", error was " << strerror(error_num);
      return false;
    }
  }
  return true;
}

bool CleanPriorFiles(const std::vector<std::string>& paths, const std::set<std::string>& preserving) {
  std::string prior_files;
  for (auto path : paths) {
    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) < 0 && errno != ENOENT) {
      // If ENOENT, it doesn't exist yet, so there is no work to do'
      int error_num = errno;
      LOG(ERROR) << "Could not stat \"" << path << "\": " << strerror(error_num);
      return false;
    }
    bool is_directory = (statbuf.st_mode & S_IFMT) == S_IFDIR;
    prior_files += (is_directory ? (path + "/*") : path) + " ";
  }
  LOG(DEBUG) << "Assuming prior files of " << prior_files;
  std::string lsof_cmd = "lsof -t " + prior_files + " >/dev/null 2>&1";
  int rval = std::system(lsof_cmd.c_str());
  // lsof returns 0 if any of the files are open
  if (WEXITSTATUS(rval) == 0) {
    LOG(ERROR) << "Clean aborted: files are in use";
    return false;
  }
  for (const auto& path : paths) {
    if (!CleanPriorFiles(path, preserving)) {
      LOG(ERROR) << "Remove of file under \"" << path << "\" failed";
      return false;
    }
  }
  return true;
}

bool CleanPriorFiles(const cuttlefish::CuttlefishConfig& config, const std::set<std::string>& preserving) {
  std::vector<std::string> paths = {
    // Everything in the assembly directory
    FLAGS_assembly_dir,
    // The environment file
    GetCuttlefishEnvPath(),
    // The global link to the config file
    cuttlefish::GetGlobalConfigFileLink(),
  };
  for (const auto& instance : config.Instances()) {
    paths.push_back(instance.instance_dir());
  }
  paths.push_back(FLAGS_instance_dir);
  return CleanPriorFiles(paths, preserving);
}

bool DecompressKernel(const std::string& src, const std::string& dst) {
  cuttlefish::Command decomp_cmd(cuttlefish::DefaultHostArtifactsPath("bin/extract-vmlinux"));
  decomp_cmd.AddParameter(src);
  std::string current_path = getenv("PATH") == nullptr ? "" : getenv("PATH");
  std::string bin_folder = cuttlefish::DefaultHostArtifactsPath("bin");
  decomp_cmd.SetEnvironment({"PATH=" + current_path + ":" + bin_folder});
  auto output_file = cuttlefish::SharedFD::Creat(dst.c_str(), 0666);
  if (!output_file->IsOpen()) {
    LOG(ERROR) << "Unable to create decompressed image file: "
               << output_file->StrError();
    return false;
  }
  decomp_cmd.RedirectStdIO(cuttlefish::Subprocess::StdIOChannel::kStdOut, output_file);
  auto decomp_proc = decomp_cmd.Start();
  return decomp_proc.Started() && decomp_proc.Wait() == 0;
}

void ValidateAdbModeFlag(const cuttlefish::CuttlefishConfig& config) {
  auto adb_modes = config.adb_mode();
  adb_modes.erase(cuttlefish::AdbMode::Unknown);
  if (adb_modes.size() < 1) {
    LOG(INFO) << "ADB not enabled";
  }
}

} // namespace

namespace {

std::vector<ImagePartition> disk_config() {
  std::vector<ImagePartition> partitions;

  // Note that if the positions of env or misc change, the environment for
  // u-boot must be updated as well (see boot_config.cc and
  // configs/cf-x86_defconfig in external/u-boot).
  partitions.push_back(ImagePartition {
    .label = "env",
    .image_file_path = FLAGS_boot_env_image,
  });
  partitions.push_back(ImagePartition {
    .label = "misc",
    .image_file_path = FLAGS_misc_image,
  });
  partitions.push_back(ImagePartition {
    .label = "boot_a",
    .image_file_path = FLAGS_boot_image,
  });
  partitions.push_back(ImagePartition {
    .label = "boot_b",
    .image_file_path = FLAGS_boot_image,
  });
  partitions.push_back(ImagePartition {
    .label = "vendor_boot_a",
    .image_file_path = FLAGS_vendor_boot_image,
  });
  partitions.push_back(ImagePartition {
    .label = "vendor_boot_b",
    .image_file_path = FLAGS_vendor_boot_image,
  });
  partitions.push_back(ImagePartition {
    .label = "vbmeta_a",
    .image_file_path = FLAGS_vbmeta_image,
  });
  partitions.push_back(ImagePartition {
    .label = "vbmeta_b",
    .image_file_path = FLAGS_vbmeta_image,
  });
  partitions.push_back(ImagePartition {
    .label = "vbmeta_system_a",
    .image_file_path = FLAGS_vbmeta_system_image,
  });
  partitions.push_back(ImagePartition {
    .label = "vbmeta_system_b",
    .image_file_path = FLAGS_vbmeta_system_image,
  });
  partitions.push_back(ImagePartition {
    .label = "super",
    .image_file_path = FLAGS_super_image,
  });
  partitions.push_back(ImagePartition {
    .label = "userdata",
    .image_file_path = FLAGS_data_image,
  });
  partitions.push_back(ImagePartition {
    .label = "cache",
    .image_file_path = FLAGS_cache_image,
  });
  partitions.push_back(ImagePartition {
    .label = "metadata",
    .image_file_path = FLAGS_metadata_image,
  });
  return partitions;
}

std::chrono::system_clock::time_point LastUpdatedInputDisk() {
  std::chrono::system_clock::time_point ret;
  for (auto& partition : disk_config()) {
    auto partition_mod_time = cuttlefish::FileModificationTime(partition.image_file_path);
    if (partition_mod_time > ret) {
      ret = partition_mod_time;
    }
  }
  return ret;
}

bool ShouldCreateCompositeDisk(const cuttlefish::CuttlefishConfig& config) {
  if (!cuttlefish::FileExists(config.composite_disk_path())) {
    return true;
  }
  auto composite_age = cuttlefish::FileModificationTime(config.composite_disk_path());
  return composite_age < LastUpdatedInputDisk();
}

bool ConcatRamdisks(const std::string& new_ramdisk_path, const std::string& ramdisk_a_path,
  const std::string& ramdisk_b_path) {
  // clear out file of any pre-existing content
  std::ofstream new_ramdisk(new_ramdisk_path, std::ios_base::binary | std::ios_base::trunc);
  std::ifstream ramdisk_a(ramdisk_a_path, std::ios_base::binary);
  std::ifstream ramdisk_b(ramdisk_b_path, std::ios_base::binary);

  if(!new_ramdisk.is_open() || !ramdisk_a.is_open() || !ramdisk_b.is_open()) {
    return false;
  }

  new_ramdisk << ramdisk_a.rdbuf() << ramdisk_b.rdbuf();
  return true;
}

off_t AvailableSpaceAtPath(const std::string& path) {
  struct statvfs vfs;
  if (statvfs(path.c_str(), &vfs) != 0) {
    int error_num = errno;
    LOG(ERROR) << "Could not find space available at " << path << ", error was "
               << strerror(error_num);
    return 0;
  }
  return vfs.f_bsize * vfs.f_bavail; // block size * free blocks for unprivileged users
}

bool CreateCompositeDisk(const cuttlefish::CuttlefishConfig& config) {
  if (!cuttlefish::SharedFD::Open(config.composite_disk_path().c_str(), O_WRONLY | O_CREAT, 0644)->IsOpen()) {
    LOG(ERROR) << "Could not ensure " << config.composite_disk_path() << " exists";
    return false;
  }
  if (FLAGS_vm_manager == CrosvmManager::name()) {
    // Check if filling in the sparse image would run out of disk space.
    auto existing_sizes = cuttlefish::SparseFileSizes(FLAGS_data_image);
    if (existing_sizes.sparse_size == 0 && existing_sizes.disk_size == 0) {
      LOG(ERROR) << "Unable to determine size of \"" << FLAGS_data_image
                 << "\". Does this file exist?";
    }
    auto available_space = AvailableSpaceAtPath(FLAGS_data_image);
    if (available_space < existing_sizes.sparse_size - existing_sizes.disk_size) {
      // TODO(schuffelen): Duplicate this check in run_cvd when it can run on a separate machine
      LOG(ERROR) << "Not enough space remaining in fs containing " << FLAGS_data_image;
      LOG(ERROR) << "Wanted " << (existing_sizes.sparse_size - existing_sizes.disk_size);
      LOG(ERROR) << "Got " << available_space;
      return false;
    } else {
      LOG(DEBUG) << "Available space: " << available_space;
      LOG(DEBUG) << "Sparse size of \"" << FLAGS_data_image << "\": "
                 << existing_sizes.sparse_size;
      LOG(DEBUG) << "Disk size of \"" << FLAGS_data_image << "\": "
                 << existing_sizes.disk_size;
    }
    std::string header_path = config.AssemblyPath("gpt_header.img");
    std::string footer_path = config.AssemblyPath("gpt_footer.img");
    CreateCompositeDisk(disk_config(), header_path, footer_path,
                        config.composite_disk_path());
  } else {
    // If this doesn't fit into the disk, it will fail while aggregating. The
    // aggregator doesn't maintain any sparse attributes.
    AggregateImage(disk_config(), config.composite_disk_path());
  }
  return true;
}

} // namespace

#ifndef O_TMPFILE
# define O_TMPFILE (020000000 | O_DIRECTORY)
#endif

const cuttlefish::CuttlefishConfig* InitFilesystemAndCreateConfig(
    int* argc, char*** argv, cuttlefish::FetcherConfig fetcher_config) {
  if (!ParseCommandLineFlags(argc, argv)) {
    LOG(ERROR) << "Failed to parse command arguments";
    exit(AssemblerExitCodes::kArgumentParsingError);
  }

  std::string assembly_dir_parent = cuttlefish::AbsolutePath(FLAGS_assembly_dir);
  while (assembly_dir_parent[assembly_dir_parent.size() - 1] == '/') {
    assembly_dir_parent =
        assembly_dir_parent.substr(0, FLAGS_assembly_dir.rfind('/'));
  }
  assembly_dir_parent =
      assembly_dir_parent.substr(0, FLAGS_assembly_dir.rfind('/'));
  auto log =
      cuttlefish::SharedFD::Open(
          assembly_dir_parent,
          O_WRONLY | O_TMPFILE,
          S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (!log->IsOpen()) {
    LOG(ERROR) << "Could not open O_TMPFILE precursor to assemble_cvd.log: "
               << log->StrError();
  } else {
    android::base::SetLogger(cuttlefish::TeeLogger({
      {cuttlefish::ConsoleSeverity(), cuttlefish::SharedFD::Dup(2)},
      {cuttlefish::LogFileSeverity(), log},
    }));
  }

  auto boot_img_unpacker =
    cuttlefish::BootImageUnpacker::FromImages(FLAGS_boot_image,
                                      FLAGS_vendor_boot_image);
  {
    // The config object is created here, but only exists in memory until the
    // SaveConfig line below. Don't launch cuttlefish subprocesses between these
    // two operations, as those will assume they can read the config object from
    // disk.
    auto config = InitializeCuttlefishConfiguration(*boot_img_unpacker, fetcher_config);
    std::set<std::string> preserving;
    if (FLAGS_resume && ShouldCreateCompositeDisk(config)) {
      LOG(INFO) << "Requested resuming a previous session (the default behavior) "
                << "but the base images have changed under the overlay, making the "
                << "overlay incompatible. Wiping the overlay files.";
    } else if (FLAGS_resume && !ShouldCreateCompositeDisk(config)) {
      preserving.insert("overlay.img");
      preserving.insert("gpt_header.img");
      preserving.insert("gpt_footer.img");
      preserving.insert("composite.img");
      preserving.insert("sdcard.img");
      preserving.insert("access-kregistry");
      preserving.insert("disk_hole");
      preserving.insert("NVChip");
    }
    if (!CleanPriorFiles(config, preserving)) {
      LOG(ERROR) << "Failed to clean prior files";
      exit(AssemblerExitCodes::kPrioFilesCleanupError);
    }
    // Create assembly directory if it doesn't exist.
    if (!cuttlefish::DirectoryExists(FLAGS_assembly_dir.c_str())) {
      LOG(DEBUG) << "Setting up " << FLAGS_assembly_dir;
      if (mkdir(FLAGS_assembly_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0
          && errno != EEXIST) {
        LOG(ERROR) << "Failed to create assembly directory: "
                  << FLAGS_assembly_dir << ". Error: " << errno;
        exit(AssemblerExitCodes::kAssemblyDirCreationError);
      }
    }
    if (log->LinkAtCwd(config.AssemblyPath("assemble_cvd.log"))) {
      LOG(ERROR) << "Unable to persist assemble_cvd log at "
                  << config.AssemblyPath("assemble_cvd.log")
                  << ": " << log->StrError();
    }
    std::string disk_hole_dir = FLAGS_assembly_dir + "/disk_hole";
    if (!cuttlefish::DirectoryExists(disk_hole_dir.c_str())) {
      LOG(DEBUG) << "Setting up " << disk_hole_dir << "/disk_hole";
      if (mkdir(disk_hole_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0
          && errno != EEXIST) {
        LOG(ERROR) << "Failed to create assembly directory: "
                  << disk_hole_dir << ". Error: " << errno;
        exit(AssemblerExitCodes::kAssemblyDirCreationError);
      }
    }
    for (const auto& instance : config.Instances()) {
      // Create instance directory if it doesn't exist.
      if (!cuttlefish::DirectoryExists(instance.instance_dir().c_str())) {
        LOG(DEBUG) << "Setting up " << FLAGS_instance_dir << ".N";
        if (mkdir(instance.instance_dir().c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0
            && errno != EEXIST) {
          LOG(ERROR) << "Failed to create instance directory: "
                    << FLAGS_instance_dir << ". Error: " << errno;
          exit(AssemblerExitCodes::kInstanceDirCreationError);
        }
      }
      auto internal_dir = instance.instance_dir() + "/" + cuttlefish::kInternalDirName;
      if (!cuttlefish::DirectoryExists(internal_dir)) {
        if (mkdir(internal_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0
           && errno != EEXIST) {
          LOG(ERROR) << "Failed to create internal instance directory: "
                    << internal_dir << ". Error: " << errno;
          exit(AssemblerExitCodes::kInstanceDirCreationError);
        }
      }
      auto log_path = config.AssemblyPath("assemble_cvd.log");
      auto instance_log_path = instance.PerInstancePath("assemble_cvd.log");
      if (symlink(log_path.c_str(), instance_log_path.c_str())) {
        LOG(WARNING) << "Unable to symlink " << log_path << " to "
                     << instance_log_path;
      }
    }
    if (!SaveConfig(config)) {
      LOG(ERROR) << "Failed to initialize configuration";
      exit(AssemblerExitCodes::kCuttlefishConfigurationInitError);
    }
  }

  std::string first_instance = FLAGS_instance_dir + "." + std::to_string(cuttlefish::GetInstance());
  if (symlink(first_instance.c_str(), FLAGS_instance_dir.c_str()) < 0) {
    LOG(ERROR) << "Could not symlink \"" << first_instance << "\" to \"" << FLAGS_instance_dir << "\"";
    exit(cuttlefish::kCuttlefishConfigurationInitError);
  }

  if (!cuttlefish::FileHasContent(FLAGS_boot_image)) {
    LOG(ERROR) << "File not found: " << FLAGS_boot_image;
    exit(cuttlefish::kCuttlefishConfigurationInitError);
  }

  if (!cuttlefish::FileHasContent(FLAGS_vendor_boot_image)) {
    LOG(ERROR) << "File not found: " << FLAGS_vendor_boot_image;
    exit(cuttlefish::kCuttlefishConfigurationInitError);
  }

  // Do this early so that the config object is ready for anything that needs it
  auto config = cuttlefish::CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Failed to obtain config singleton";
    exit(AssemblerExitCodes::kCuttlefishConfigurationInitError);
  }

  if (!boot_img_unpacker->Unpack(config->ramdisk_image_path(),
                                 config->vendor_ramdisk_image_path(),
                                 config->use_unpacked_kernel()
                                     ? config->kernel_image_path()
                                     : "")) {
    LOG(ERROR) << "Failed to unpack boot image";
    exit(AssemblerExitCodes::kBootImageUnpackError);
  }

  // TODO(134522463) as part of the bootloader refactor, repack the vendor boot
  // image and use the bootloader to load both the boot and vendor ramdisk.
  // Until then, this hack to get gki modules into cuttlefish will suffice.

  // If a vendor ramdisk comes in via this mechanism, let it supercede the one
  // in the vendor boot image. This flag is what kernel presubmit testing uses
  // to pass in the kernel ramdisk.

  // If no kernel is passed in or an initramfs is made available, the default
  // vendor boot ramdisk or the initramfs provided should be appended to the
  // boot ramdisk. If a kernel IS provided with no initramfs, it is safe to
  // safe to assume that the kernel was built with no modules and expects no
  // modules for cf to run properly.
  std::string discovered_kernel = fetcher_config.FindCvdFileWithSuffix(kKernelDefaultPath);
  std::string foreign_kernel = FLAGS_kernel_path.size() ? FLAGS_kernel_path : discovered_kernel;
  std::string discovered_ramdisk = fetcher_config.FindCvdFileWithSuffix(kInitramfsImg);
  std::string foreign_ramdisk = FLAGS_initramfs_path.size () ? FLAGS_initramfs_path : discovered_ramdisk;
  if(!foreign_kernel.size() || foreign_ramdisk.size()) {
    const std::string& vendor_ramdisk_path =
      config->initramfs_path().size() ? config->initramfs_path()
                                      : config->vendor_ramdisk_image_path();
    if(!ConcatRamdisks(config->final_ramdisk_path(),
                       config->ramdisk_image_path(), vendor_ramdisk_path)) {
      LOG(ERROR) << "Failed to concatenate ramdisk and vendor ramdisk";
      exit(AssemblerExitCodes::kInitRamFsConcatError);
    }
  }

  if (config->decompress_kernel()) {
    if (!DecompressKernel(config->kernel_image_path(),
        config->decompressed_kernel_image_path())) {
      LOG(ERROR) << "Failed to decompress kernel";
      exit(AssemblerExitCodes::kKernelDecompressError);
    }
  }

  ValidateAdbModeFlag(*config);

  // Create misc if necessary
  if (!InitializeMiscImage(FLAGS_misc_image)) {
    exit(cuttlefish::kCuttlefishConfigurationInitError);
  }

  // Create data if necessary
  DataImageResult dataImageResult = ApplyDataImagePolicy(*config, FLAGS_data_image);
  if (dataImageResult == DataImageResult::Error) {
    exit(cuttlefish::kCuttlefishConfigurationInitError);
  }

  // Create boot_config if necessary
  if (!InitBootloaderEnvPartition(*config, FLAGS_boot_env_image)) {
    exit(cuttlefish::kCuttlefishConfigurationInitError);
  }

  if (!cuttlefish::FileExists(FLAGS_metadata_image)) {
    CreateBlankImage(FLAGS_metadata_image, FLAGS_blank_metadata_image_mb, "none");
  }

  for (const auto& instance : config->Instances()) {
    if (!cuttlefish::FileExists(instance.access_kregistry_path())) {
      CreateBlankImage(instance.access_kregistry_path(), 2 /* mb */, "none");
    }

    if (!cuttlefish::FileExists(instance.pstore_path())) {
      CreateBlankImage(instance.pstore_path(), 2 /* mb */, "none");
    }

    if (!cuttlefish::FileExists(instance.sdcard_path())) {
      CreateBlankImage(instance.sdcard_path(),
                       FLAGS_blank_sdcard_image_mb, "sdcard");
    }
  }

  // libavb expects to be able to read the maximum vbmeta size, so we must
  // provide a partition which matches this or the read will fail
  for (const auto& vbmeta_image : { FLAGS_vbmeta_image, FLAGS_vbmeta_system_image }) {
    if (cuttlefish::FileSize(vbmeta_image) != VBMETA_MAX_SIZE) {
      auto fd = cuttlefish::SharedFD::Open(vbmeta_image, O_RDWR);
      if (fd->Truncate(VBMETA_MAX_SIZE) != 0) {
        LOG(ERROR) << "`truncate --size=" << VBMETA_MAX_SIZE << " "
                   << vbmeta_image << "` failed: " << fd->StrError();
        exit(cuttlefish::kCuttlefishConfigurationInitError);
      }
    }
  }

  if (SuperImageNeedsRebuilding(fetcher_config, *config)) {
    if (!RebuildSuperImage(fetcher_config, *config, FLAGS_super_image)) {
      LOG(ERROR) << "Super image rebuilding requested but could not be completed.";
      exit(cuttlefish::kCuttlefishConfigurationInitError);
    }
  }

  bool oldCompositeDisk = ShouldCreateCompositeDisk(*config);
  bool newDataImage = dataImageResult == DataImageResult::FileUpdated;
  if (oldCompositeDisk || newDataImage) {
    if (!CreateCompositeDisk(*config)) {
      exit(cuttlefish::kDiskSpaceError);
    }
  }

  for (auto instance : config->Instances()) {
    auto overlay_path = instance.PerInstancePath("overlay.img");
    bool missingOverlay = !cuttlefish::FileExists(overlay_path);
    bool newOverlay = cuttlefish::FileModificationTime(overlay_path)
        < cuttlefish::FileModificationTime(config->composite_disk_path());
    if (missingOverlay || oldCompositeDisk || !FLAGS_resume || newDataImage || newOverlay) {
      if (FLAGS_resume) {
        LOG(INFO) << "Requested to continue an existing session, (the default) "
                  << "but the disk files have become out of date. Wiping the "
                  << "old session files and starting a new session.";
      }
      CreateQcowOverlay(config->crosvm_binary(), config->composite_disk_path(), overlay_path);
      CreateBlankImage(instance.access_kregistry_path(), 2 /* mb */, "none");
      CreateBlankImage(instance.pstore_path(), 2 /* mb */, "none");
    }
  }

  for (auto instance : config->Instances()) {
    // Check that the files exist
    for (const auto& file : instance.virtual_disk_paths()) {
      if (!file.empty() && !cuttlefish::FileHasContent(file.c_str())) {
        LOG(ERROR) << "File not found: " << file;
        exit(cuttlefish::kCuttlefishConfigurationInitError);
      }
    }
  }

  return config;
}

std::string GetConfigFilePath(const cuttlefish::CuttlefishConfig& config) {
  return config.AssemblyPath("cuttlefish_config.json");
}
