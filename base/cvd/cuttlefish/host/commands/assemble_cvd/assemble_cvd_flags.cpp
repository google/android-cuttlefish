/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/assemble_cvd_flags.h"

#include <string>

#include <fmt/format.h>
#include <gflags/gflags.h>

#include "cuttlefish/common/libs/utils/known_paths.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/host/libs/config/display.h"

#define DEFINE_vec DEFINE_string

DEFINE_vec(cpus, std::to_string(CF_DEFAULTS_CPUS),
              "Virtual CPU count.");
DEFINE_vec(data_policy, CF_DEFAULTS_DATA_POLICY,
              "How to handle userdata partition."
              " Either 'use_existing', 'create_if_missing', 'resize_up_to', or "
              "'always_create'.");
DEFINE_vec(blank_data_image_mb,
              CF_DEFAULTS_BLANK_DATA_IMAGE_MB,
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
DEFINE_string(overlays, "",
              "List of displays to overlay. Format is: 'vm_index:display_index "
              "vm_index2:display_index2 [...]'");
DEFINE_string(extra_kernel_cmdline, CF_DEFAULTS_EXTRA_KERNEL_CMDLINE,
              "Additional flags to put on the kernel command line");
DEFINE_string(extra_bootconfig_args, CF_DEFAULTS_EXTRA_BOOTCONFIG_ARGS,
              "Space-separated list of extra bootconfig args. "
              "Note: overwriting an existing bootconfig argument "
              "requires ':=' instead of '='.");
DEFINE_vec(guest_enforce_security,
           fmt::format("{}", CF_DEFAULTS_GUEST_ENFORCE_SECURITY),
           "Whether to run in enforcing mode (non permissive).");
DEFINE_vec(memory_mb, std::to_string(CF_DEFAULTS_MEMORY_MB),
             "Total amount of memory available for guest, MB.");
DEFINE_vec(serial_number, CF_DEFAULTS_SERIAL_NUMBER,
              "Serial number to use for the device");
DEFINE_vec(use_random_serial, fmt::format("{}", CF_DEFAULTS_USE_RANDOM_SERIAL),
           "Whether to use random serial for the device.");
DEFINE_vec(gpu_mode, CF_DEFAULTS_GPU_MODE,
           "What gpu configuration to use, one of {auto, custom, drm_virgl, "
           "gfxstream, gfxstream_guest_angle, "
           "gfxstream_guest_angle_host_swiftshader, "
           "gfxstream_guest_angle_host_lavapipe, guest_swiftshader}");
DEFINE_vec(gpu_vhost_user_mode,
           fmt::format("{}", CF_DEFAULTS_GPU_VHOST_USER_MODE),
           "Whether or not to run the Virtio GPU worker in a separate"
           "process using vhost-user-gpu. One of {auto, on, off}.");
DEFINE_vec(hwcomposer, CF_DEFAULTS_HWCOMPOSER,
              "What hardware composer to use, one of {auto, drm, ranchu} ");
DEFINE_vec(gpu_capture_binary, CF_DEFAULTS_GPU_CAPTURE_BINARY,
              "Path to the GPU capture binary to use when capturing GPU traces"
              "(ngfx, renderdoc, etc)");
DEFINE_vec(enable_gpu_udmabuf,
           fmt::format("{}", CF_DEFAULTS_ENABLE_GPU_UDMABUF),
           "Use the udmabuf driver for zero-copy virtio-gpu");
DEFINE_vec(
    gpu_renderer_features, CF_DEFAULTS_GPU_RENDERER_FEATURES,
    "Renderer specific features to enable. For Gfxstream, this should "
    "be a semicolon separated list of \"<feature name>:[enabled|disabled]\""
    "pairs.");

DEFINE_vec(gpu_context_types, CF_DEFAULTS_GPU_CONTEXT_TYPES,
           "A colon separated list of virtio-gpu context types.  Only valid "
           "with --gpu_mode=custom."
           " For example \"--gpu_context_types=cross_domain:gfxstream\"");

DEFINE_vec(
    guest_hwui_renderer, CF_DEFAULTS_GUEST_HWUI_RENDERER,
    "The default renderer that HWUI should use, one of {skiagl, skiavk}.");

DEFINE_vec(guest_renderer_preload, CF_DEFAULTS_GUEST_RENDERER_PRELOAD,
           "Whether or not Zygote renderer preload is disabled, one of {auto, "
           "enabled, disabled}. Auto will choose whether or not to disable "
           "based on the gpu mode and guest hwui renderer.");

DEFINE_vec(
    guest_vulkan_driver, CF_DEFAULTS_GUEST_VULKAN_DRIVER,
    "Vulkan driver to use with Cuttlefish.  Android VMs require specifying "
    "this at boot time.  Only valid with --gpu_mode=custom. "
    "For example \"--guest_vulkan_driver=ranchu\"");

DEFINE_vec(
    frames_socket_path, CF_DEFAULTS_FRAME_SOCKET_PATH,
    "Frame socket path to use when launching a VM "
    "For example, \"--frames_socket_path=${XDG_RUNTIME_DIR}/wayland-0\"");

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
            "Enable the rootcanal which is Bluetooth emulator in the host.");
DEFINE_int32(
    rootcanal_instance_num, CF_DEFAULTS_ROOTCANAL_INSTANCE_NUM,
    "If it is greater than 0, use an existing rootcanal instance which is "
    "launched from cuttlefish instance "
    "with rootcanal_instance_num. Else, launch a new rootcanal instance");
DEFINE_string(rootcanal_args, CF_DEFAULTS_ROOTCANAL_ARGS,
              "Space-separated list of rootcanal args. ");
DEFINE_bool(enable_host_nfc, CF_DEFAULTS_ENABLE_HOST_NFC,
            "Enable the NFC emulator in the host.");
DEFINE_int32(
    casimir_instance_num, CF_DEFAULTS_CASIMIR_INSTANCE_NUM,
    "If it is greater than 0, use an existing casimir instance which is "
    "launched from cuttlefish instance "
    "with casimir_instance_num. Else, launch a new casimir instance");
DEFINE_string(casimir_args, CF_DEFAULTS_CASIMIR_ARGS,
              "Space-separated list of casimir args.");
DEFINE_bool(enable_host_uwb, CF_DEFAULTS_ENABLE_HOST_UWB,
            "Enable the uwb host and the uwb connector.");
DEFINE_int32(
    pica_instance_num, CF_DEFAULTS_ENABLE_PICA_INSTANCE_NUM,
    "If it is greater than 0, use an existing pica instance which is "
    "launched from cuttlefish instance "
    "with pica_instance_num. Else, launch a new pica instance");
DEFINE_bool(netsim, CF_DEFAULTS_NETSIM,
            "[Experimental] Connect all radios to netsim.");

DEFINE_bool(netsim_bt, CF_DEFAULTS_NETSIM_BT,
            "Connect Bluetooth radio to netsim.");
DEFINE_bool(netsim_uwb, CF_DEFAULTS_NETSIM_UWB,
            "[Experimental] Connect Uwb radio to netsim.");
DEFINE_string(netsim_args, CF_DEFAULTS_NETSIM_ARGS,
              "Space-separated list of netsim args.");

DEFINE_bool(enable_automotive_proxy, CF_DEFAULTS_ENABLE_AUTOMOTIVE_PROXY,
            "Enable the automotive proxy service on the host.");

DEFINE_bool(enable_vhal_proxy_server, CF_DEFAULTS_ENABLE_VHAL_PROXY_SERVER,
            "Enable the vhal proxy service on the host.");
DEFINE_int32(vhal_proxy_server_instance_num,
             CF_DEFAULTS_VHAL_PROXY_SERVER_INSTANCE_NUM,
             "If it is greater than 0, use an existing vhal proxy server "
             "instance which is "
             "launched from cuttlefish instance "
             "with vhal_proxy_server_instance_num. Else, launch a new vhal "
             "proxy server instance");

/**
 * crosvm sandbox feature requires /var/empty and seccomp directory
 *
 * Also see SetDefaultFlagsForCrosvm()
 */
DEFINE_vec(
    enable_sandbox, fmt::format("{}", CF_DEFAULTS_ENABLE_SANDBOX),
    "Enable crosvm sandbox assuming /var/empty and seccomp directories exist. "
    "--noenable-sandbox will disable crosvm sandbox. "
    "When no option is given, sandbox is disabled if Cuttlefish is running "
    "inside a container, or if GPU is enabled (b/152323505), "
    "or if the empty /var/empty directory either does not exist and "
    "cannot be created. Otherwise, sandbox is enabled on the supported "
    "architecture when no option is given.");

DEFINE_vec(enable_virtiofs, fmt::format("{}", CF_DEFAULTS_ENABLE_VIRTIOFS),
           "Enable shared folder using virtiofs");

DEFINE_string(
    seccomp_policy_dir, CF_DEFAULTS_SECCOMP_POLICY_DIR,
    "With sandbox'ed crosvm, overrieds the security comp policy directory");

DEFINE_vec(start_webrtc, fmt::format("{}", CF_DEFAULTS_START_WEBRTC),
           "(Deprecated, webrtc is enabled depending on the VMM)");

DEFINE_vec(webrtc_assets_dir, CF_DEFAULTS_WEBRTC_ASSETS_DIR,
           "Path to WebRTC webpage assets.");

DEFINE_string(webrtc_sig_server_addr, CF_DEFAULTS_WEBRTC_SIG_SERVER_ADDR,
              "The address of the webrtc signaling server.");

// TODO (jemoreira): We need a much bigger range to reliably support several
// simultaneous connections.
DEFINE_vec(tcp_port_range, CF_DEFAULTS_TCP_PORT_RANGE,
              "The minimum and maximum TCP port numbers to allocate for ICE "
              "candidates as 'min:max'. To use any port just specify '0:0'");

DEFINE_vec(udp_port_range, CF_DEFAULTS_UDP_PORT_RANGE,
              "The minimum and maximum UDP port numbers to allocate for ICE "
              "candidates as 'min:max'. To use any port just specify '0:0'");

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
           fmt::format("{}", CF_DEFAULTS_ENABLE_BOOTANIMATION),
           "Whether to enable the boot animation.");

DEFINE_vec(extra_bootconfig_args_base64, CF_DEFAULTS_EXTRA_BOOTCONFIG_ARGS,
           "This is base64 encoded version of extra_bootconfig_args"
           "Used for multi device clusters.");

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
           fmt::format("{}", CF_DEFAULTS_RESTART_SUBPROCESSES),
           "Restart any crashed host process");
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
DEFINE_vec(kgdb, fmt::format("{}", CF_DEFAULTS_KGDB),
           "Configure the virtual device for debugging the kernel "
           "with kgdb/kdb. The kernel must have been built with "
           "kgdb support, and serial console must be enabled.");

DEFINE_vec(start_gnss_proxy, fmt::format("{}", CF_DEFAULTS_START_GNSS_PROXY),
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

DEFINE_vec(console, fmt::format("{}", CF_DEFAULTS_CONSOLE),
           "Enable the serial console");

DEFINE_vec(enable_kernel_log, fmt::format("{}", CF_DEFAULTS_ENABLE_KERNEL_LOG),
           "Enable kernel console/dmesg logging");

DEFINE_vec(vhost_net, fmt::format("{}", CF_DEFAULTS_VHOST_NET),
           "Enable vhost acceleration of networking");

DEFINE_vec(vhost_user_vsock, fmt::format("{}", CF_DEFAULTS_VHOST_USER_VSOCK),
           "Enable vhost-user-vsock");

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

DEFINE_vec(record_screen, fmt::format("{}", CF_DEFAULTS_RECORD_SCREEN),
           "Enable screen recording. "
           "Requires --start_webrtc");

DEFINE_vec(smt, fmt::format("{}", CF_DEFAULTS_SMT),
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

DEFINE_vec(
    vsock_guest_group, CF_DEFAULTS_VSOCK_GUEST_GROUP,
    "vsock_guest_group is used to determine the guest vsock isolation groups."
    "vsock communications can only happen between VMs which are tagged with "
    "the same group name, or between VMs which have no group assigned.");

DEFINE_string(secure_hals, CF_DEFAULTS_SECURE_HALS,
              "Which HALs to use enable host security features for. Supports "
              "keymint and gatekeeper at the moment.");

DEFINE_vec(use_sdcard, CF_DEFAULTS_USE_SDCARD?"true":"false",
            "Create blank SD-Card image and expose to guest");

DEFINE_vec(protected_vm, fmt::format("{}", CF_DEFAULTS_PROTECTED_VM),
           "Boot in Protected VM mode");

DEFINE_vec(mte, fmt::format("{}", CF_DEFAULTS_MTE), "Enable MTE");

DEFINE_vec(enable_audio, fmt::format("{}", CF_DEFAULTS_ENABLE_AUDIO),
           "Whether to play or capture audio");

DEFINE_vec(enable_usb, fmt::format("{}", CF_DEFAULTS_ENABLE_USB),
           "Whether to allow USB passthrough on the device");

DEFINE_vec(enable_jcard_simulator,
           fmt::format("{}", CF_DEFAULTS_ENABLE_JCARD_SIMULATOR),
           "Whether to allow host jcard simulator on the device");

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

DEFINE_bool(track_host_tools_crc, CF_DEFAULTS_TRACK_HOST_TOOLS_CRC,
            "Track changes to host executables");

// The default value should be set to the default of crosvm --balloon
DEFINE_vec(crosvm_use_balloon, "true",
           "Controls the crosvm --no-balloon flag"
           "The flag is given if crosvm_use_balloon is false");

DEFINE_vec(crosvm_use_rng, "true",
           "Controls the crosvm --no-rng flag"
           "The flag is given if crosvm_use_rng is false");

DEFINE_vec(crosvm_simple_media_device, "false",
           "Controls the crosvm --simple-media-device flag"
           "The flag is given if crosvm_simple_media_device is true.");

DEFINE_vec(crosvm_v4l2_proxy, CF_DEFAULTS_CROSVM_V4L2_PROXY,
           "Controls the crosvm --v4l2-proxy flag"
           "The flag is given if crosvm_v4l2_proxy is set with a valid string literal. "
           "When this flag is set, crosvm_simple_media_device becomes ineffective.");

DEFINE_vec(use_pmem, "true",
           "Make this flag false to disable pmem with crosvm");

DEFINE_bool(enable_wifi, true, "Enables the guest WIFI. Mainly for Minidroid");

DEFINE_vec(device_external_network, CF_DEFAULTS_DEVICE_EXTERNAL_NETWORK,
           "The mechanism to connect to the public internet.");

// disable wifi, disable sandbox, use guest_swiftshader
DEFINE_bool(snapshot_compatible, false,
            "Declaring that device is snapshot'able and runs with only "
            "supported ones.");

DEFINE_vec(mcu_config_path, CF_DEFAULTS_MCU_CONFIG_PATH,
           "configuration file for the MCU emulator");

DEFINE_string(straced_host_executables, CF_DEFAULTS_STRACED_HOST_EXECUTABLES,
              "Comma-separated list of executable names to run under strace "
              "to collect their system call information.");

DEFINE_vec(
    fail_fast, CF_DEFAULTS_FAIL_FAST ? "true" : "false",
    "Whether to exit when a heuristic predicts the boot will not complete");

DEFINE_vec(vhost_user_block, CF_DEFAULTS_VHOST_USER_BLOCK ? "true" : "false",
           "(experimental) use crosvm vhost-user block device implementation ");

DEFINE_string(early_tmp_dir, cuttlefish::TempDir(),
              "Parent directory to use for temporary files in early startup");

DEFINE_vec(enable_tap_devices, "true",
           "TAP devices are used on linux for connecting to the network "
           "outside the current machine.");

DEFINE_vec(vcpu_config_path, CF_DEFAULTS_VCPU_CONFIG_PATH,
           "configuration file for Virtual Cpufreq");

DEFINE_string(kvm_path, "",
              "Device node file used to create VMs. Uses a default if empty.");

DEFINE_string(vhost_vsock_path, "",
              "Device node file for the kernel vhost-vsock implementation. "
              "Uses a default if empty. Ignored for QEMU.");

DEFINE_string(assembly_dir, CF_DEFAULTS_ASSEMBLY_DIR,
              "A directory to put generated files common between instances");
DEFINE_string(instance_dir, CF_DEFAULTS_INSTANCE_DIR,
              "This is a directory that will hold the cuttlefish generated"
              "files, including both instance-specific and common files");
DEFINE_string(snapshot_path, "",
              "Path to snapshot. Must not be empty if the device is to be "
              "restored from a snapshot");
DEFINE_bool(resume, CF_DEFAULTS_RESUME,
            "Resume using the disk from the last session, if "
            "possible. i.e., if --noresume is passed, the disk "
            "will be reset to the state it was initially launched "
            "in. This flag is ignored if the underlying partition "
            "images have been updated since the first launch."
            "If the device starts from a snapshot, this will be always true.");

DEFINE_string(super_image, CF_DEFAULTS_SUPER_IMAGE,
              "Location of the super partition image.");
DEFINE_string(
    vendor_boot_image, CF_DEFAULTS_VENDOR_BOOT_IMAGE,
    "Location of cuttlefish vendor boot image. If empty it is assumed to "
    "be vendor_boot.img in the directory specified by -system_image_dir.");
DEFINE_string(vbmeta_image, CF_DEFAULTS_VBMETA_IMAGE,
              "Location of cuttlefish vbmeta image. If empty it is assumed to "
              "be vbmeta.img in the directory specified by -system_image_dir.");
DEFINE_string(
    vbmeta_system_image, CF_DEFAULTS_VBMETA_SYSTEM_IMAGE,
    "Location of cuttlefish vbmeta_system image. If empty it is assumed to "
    "be vbmeta_system.img in the directory specified by -system_image_dir.");
DEFINE_string(
    vbmeta_vendor_dlkm_image, CF_DEFAULTS_VBMETA_VENDOR_DLKM_IMAGE,
    "Location of cuttlefish vbmeta_vendor_dlkm image. If empty it is assumed "
    "to "
    "be vbmeta_vendor_dlkm.img in the directory specified by "
    "-system_image_dir.");
DEFINE_string(
    vbmeta_system_dlkm_image, CF_DEFAULTS_VBMETA_SYSTEM_DLKM_IMAGE,
    "Location of cuttlefish vbmeta_system_dlkm image. If empty it is assumed "
    "to "
    "be vbmeta_system_dlkm.img in the directory specified by "
    "-system_image_dir.");
DEFINE_string(default_vvmtruststore_file_name,
              CF_DEFAULTS_DEFAULT_VVMTRUSTSTORE_FILE_NAME,
              "If the vvmtruststore_path parameter is empty then the default "
              "file name of the vvmtruststore image in the directory specified"
              " by -system_image_dir. If empty then there's no vvmtruststore "
              "image assumed by default.");
DEFINE_string(vvmtruststore_path, CF_DEFAULTS_VVMTRUSTSTORE_PATH,
              "Location of the vvmtruststore image");

DEFINE_string(
    default_target_zip, CF_DEFAULTS_DEFAULT_TARGET_ZIP,
    "Location of default target zip file.");
DEFINE_string(
    system_target_zip, CF_DEFAULTS_SYSTEM_TARGET_ZIP,
    "Location of system target zip file.");

DEFINE_string(linux_kernel_path, CF_DEFAULTS_LINUX_KERNEL_PATH,
              "Location of linux kernel for cuttlefish otheros flow.");
DEFINE_string(linux_initramfs_path, CF_DEFAULTS_LINUX_INITRAMFS_PATH,
              "Location of linux initramfs.img for cuttlefish otheros flow.");
DEFINE_string(linux_root_image, CF_DEFAULTS_LINUX_ROOT_IMAGE,
              "Location of linux root filesystem image for cuttlefish otheros flow.");

DEFINE_string(chromeos_disk, CF_DEFAULTS_CHROMEOS_DISK,
              "Location of a complete ChromeOS GPT disk");
DEFINE_string(chromeos_kernel_path, CF_DEFAULTS_CHROMEOS_KERNEL_PATH,
              "Location of the chromeos kernel for the chromeos flow.");
DEFINE_string(chromeos_root_image, CF_DEFAULTS_CHROMEOS_ROOT_IMAGE,
              "Location of chromeos root filesystem image for chromeos flow.");

DEFINE_string(fuchsia_zedboot_path, CF_DEFAULTS_FUCHSIA_ZEDBOOT_PATH,
              "Location of fuchsia zedboot path for cuttlefish otheros flow.");
DEFINE_string(fuchsia_multiboot_bin_path, CF_DEFAULTS_FUCHSIA_MULTIBOOT_BIN_PATH,
              "Location of fuchsia multiboot bin path for cuttlefish otheros flow.");
DEFINE_string(fuchsia_root_image, CF_DEFAULTS_FUCHSIA_ROOT_IMAGE,
              "Location of fuchsia root filesystem image for cuttlefish otheros flow.");

DEFINE_string(
    custom_partition_path, CF_DEFAULTS_CUSTOM_PARTITION_PATH,
    "Location of custom image that will be passed as a \"custom\" partition"
    "to rootfs and can be used by /dev/block/by-name/custom. Multiple images "
    "can be passed, separated by semicolons and can be used as "
    "/dev/block/by-name/custom_1, /dev/block/by-name/custom_2, etc. Example: "
    "--custom_partition_path=\"/path/to/custom.img;/path/to/other.img\"");

DEFINE_string(
    blank_sdcard_image_mb, CF_DEFAULTS_BLANK_SDCARD_IMAGE_MB,
    "If enabled, the size of the blank sdcard image to generate, MB.");
