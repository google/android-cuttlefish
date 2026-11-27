/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <optional>
#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/device_type.h"
#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/boot_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/kernel_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/proto/guest_config.pb.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/gpu_mode.h"
#include "cuttlefish/pretty/pretty.h"
#include "cuttlefish/pretty/struct.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct GuestConfig {
  std::string android_version_number;
  std::string ti50_emulator;
  std::vector<GpuMode> gpu_mode_candidates;
  std::vector<CuttlefishConfig::DisplayConfig> displays;
  std::vector<CuttlefishConfig::TouchpadConfig> touchpads;
  std::optional<std::string> custom_keyboard_config;
  std::optional<std::string> domkey_mapping_config;
  std::optional<::cuttlefish::config::Audio> audio_settings;
  Arch target_arch;
  DeviceType device_type;
  int output_audio_streams_count = 1;
  int blank_data_image_mb = 0;
  std::optional<int> memory_mb;
  bool bootconfig_supported = false;
  bool hctr2_supported = false;
  bool gfxstream_supported = false;
  bool gfxstream_gl_program_binary_link_status_supported = false;
  bool guest_lavapipe_supported = false;
  bool venus_supported = false;
  bool vhost_user_vsock = false;
  bool supports_bgra_framebuffers = false;
  bool mouse_supported = false;
  bool gamepad_supported = false;
  bool lights_server_enabled = true;  // true for backwards compatibility
  bool has_vulkan_lavapipe_apex = false;
  bool has_vulkan_gfxstream_apex = false;
  bool has_vulkan_swiftshader_apex = false;
  bool has_vulkan_venus_apex = false;
  std::optional<bool> enforce_mac80211_hwsim;
  std::optional<bool> use_sdcard;
  std::optional<bool> vhal_proxy_server_enabled;
};

PrettyStruct Pretty(const GuestConfig&,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

Result<void> ParseGuestConfigTextProto(const std::string& guest_config_path,
                                       GuestConfig& guest_config);

Result<std::vector<GuestConfig>> ReadGuestConfig(
    const BootImageFlag&, const KernelPathFlag& kernel_path,
    const SystemImageDirFlag& system_image_dir);
}  // namespace cuttlefish
