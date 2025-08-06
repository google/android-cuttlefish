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

#pragma once

#include <gflags/gflags.h>

#define DECLARE_vec DECLARE_string

DECLARE_vec(cpus);
DECLARE_vec(data_policy);
DECLARE_vec(blank_data_image_mb);
DECLARE_vec(gdb_port);

// TODO(b/192495477): combine these into a single repeatable '--display' flag
// when assemble_cvd switches to using the new flag parsing library.
DECLARE_string(display0);
DECLARE_string(display1);
DECLARE_string(display2);
DECLARE_string(display3);

// TODO(b/171305898): mark these as deprecated after multi-display is fully
// enabled.
DECLARE_string(x_res);
DECLARE_string(y_res);
DECLARE_string(dpi);
DECLARE_string(refresh_rate_hz);
DECLARE_string(overlays);
DECLARE_string(extra_kernel_cmdline);
DECLARE_string(extra_bootconfig_args);
DECLARE_vec(guest_enforce_security);
DECLARE_vec(memory_mb);
DECLARE_vec(serial_number);
DECLARE_vec(use_random_serial);
DECLARE_vec(gpu_mode);
DECLARE_vec(gpu_vhost_user_mode);
DECLARE_vec(hwcomposer);
DECLARE_vec(gpu_capture_binary);
DECLARE_vec(enable_gpu_udmabuf);
DECLARE_vec(gpu_renderer_features);
DECLARE_vec(gpu_context_types);
DECLARE_vec(guest_hwui_renderer);

DECLARE_vec(guest_renderer_preload);

DECLARE_vec(guest_vulkan_driver);

DECLARE_vec(frames_socket_path);

DECLARE_vec(use_allocd);
DECLARE_vec(enable_minimal_mode);
DECLARE_vec(pause_in_bootloader);
DECLARE_bool(enable_host_bluetooth);
DECLARE_int32(rootcanal_instance_num);
DECLARE_string(rootcanal_args);
DECLARE_bool(enable_host_nfc);
DECLARE_int32(casimir_instance_num);
DECLARE_string(casimir_args);
DECLARE_bool(enable_host_uwb);
DECLARE_int32(pica_instance_num);
DECLARE_bool(netsim);

DECLARE_bool(netsim_bt);
DECLARE_bool(netsim_uwb);
DECLARE_string(netsim_args);

DECLARE_bool(enable_automotive_proxy);

DECLARE_bool(enable_vhal_proxy_server);
DECLARE_int32(vhal_proxy_server_instance_num);

/**
 * crosvm sandbox feature requires /var/empty and seccomp directory
 *
 * Also see SetDefaultFlagsForCrosvm()
 */
DECLARE_vec(enable_sandbox);

DECLARE_vec(enable_virtiofs);

DECLARE_string(seccomp_policy_dir);

DECLARE_vec(webrtc_assets_dir);

DECLARE_bool(start_webrtc_sig_server);

DECLARE_string(webrtc_sig_server_addr);

DECLARE_vec(tcp_port_range);

DECLARE_vec(udp_port_range);

DECLARE_vec(webrtc_device_id);

DECLARE_vec(uuid);
DECLARE_vec(daemon);

DECLARE_vec(setupwizard_mode);
DECLARE_vec(enable_bootanimation);

DECLARE_vec(extra_bootconfig_args_base64);

DECLARE_string(qemu_binary_dir);
DECLARE_string(crosvm_binary);
DECLARE_vec(gem5_binary_dir);
DECLARE_vec(gem5_checkpoint_dir);
DECLARE_vec(gem5_debug_file);
DECLARE_string(gem5_debug_flags);

DECLARE_vec(restart_subprocesses);
DECLARE_vec(boot_slot);
DECLARE_int32(num_instances);
DECLARE_string(instance_nums);
DECLARE_string(report_anonymous_usage_stats);
DECLARE_vec(ril_dns);
DECLARE_vec(kgdb);

DECLARE_vec(start_gnss_proxy);

DECLARE_vec(gnss_file_path);

DECLARE_vec(fixed_location_file_path);

DECLARE_vec(enable_modem_simulator);
DECLARE_vec(modem_simulator_sim_type);

DECLARE_vec(console);

DECLARE_vec(enable_kernel_log);

DECLARE_vec(vhost_net);

DECLARE_vec(vhost_user_vsock);

DECLARE_string(vhost_user_mac80211_hwsim);
DECLARE_string(wmediumd_config);

DECLARE_string(ap_rootfs_image);
DECLARE_string(ap_kernel_image);

DECLARE_vec(record_screen);

DECLARE_vec(smt);

DECLARE_vec(vsock_guest_cid);

DECLARE_vec(vsock_guest_group);

DECLARE_string(secure_hals);

DECLARE_vec(use_sdcard);

DECLARE_vec(protected_vm);

DECLARE_vec(mte);

DECLARE_vec(enable_audio);

DECLARE_vec(enable_jcard_simulator);

DECLARE_vec(enable_usb);

DECLARE_vec(camera_server_port);

DECLARE_vec(userdata_format);

DECLARE_bool(use_overlay);

DECLARE_vec(modem_simulator_count);

DECLARE_bool(track_host_tools_crc);

DECLARE_vec(crosvm_use_balloon);

DECLARE_vec(crosvm_use_rng);

DECLARE_vec(crosvm_simple_media_device);

DECLARE_vec(crosvm_v4l2_proxy);

DECLARE_vec(use_pmem);

DECLARE_bool(enable_wifi);

DECLARE_vec(device_external_network);

DECLARE_bool(snapshot_compatible);

DECLARE_vec(mcu_config_path);

DECLARE_string(straced_host_executables);

DECLARE_vec(fail_fast);

DECLARE_vec(vhost_user_block);

DECLARE_string(early_tmp_dir);

DECLARE_vec(enable_tap_devices);

DECLARE_vec(vcpu_config_path);

DECLARE_string(kvm_path);

DECLARE_string(vhost_vsock_path);

DECLARE_string(assembly_dir);
DECLARE_string(instance_dir);
DECLARE_string(snapshot_path);
DECLARE_bool(resume);

DECLARE_string(super_image);
DECLARE_string(vendor_boot_image);
DECLARE_string(vbmeta_image);
DECLARE_string(vbmeta_system_image);
DECLARE_string(vbmeta_vendor_dlkm_image);
DECLARE_string(vbmeta_system_dlkm_image);
DECLARE_string(default_vvmtruststore_file_name);
DECLARE_string(vvmtruststore_path);

DECLARE_string(default_target_zip);
DECLARE_string(system_target_zip);

DECLARE_string(linux_kernel_path);
DECLARE_string(linux_initramfs_path);
DECLARE_string(linux_root_image);

DECLARE_string(chromeos_disk);
DECLARE_string(chromeos_kernel_path);
DECLARE_string(chromeos_root_image);

DECLARE_string(fuchsia_zedboot_path);
DECLARE_string(fuchsia_multiboot_bin_path);
DECLARE_string(fuchsia_root_image);

DECLARE_string(custom_partition_path);

DECLARE_string(blank_sdcard_image_mb);
