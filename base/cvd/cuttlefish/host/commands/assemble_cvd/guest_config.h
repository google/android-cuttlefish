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

#include "common/libs/utils/architecture.h"
#include "common/libs/utils/device_type.h"

namespace cuttlefish {

struct GuestConfig {
  Arch target_arch;
  DeviceType device_type;
  bool bootconfig_supported = false;
  bool hctr2_supported = false;
  std::string android_version_number;
  bool gfxstream_supported = false;
  bool gfxstream_gl_program_binary_link_status_supported = false;
  bool vhost_user_vsock = false;
  bool supports_bgra_framebuffers = false;
  bool prefer_drm_virgl_when_supported = false;
  bool mouse_supported = false;
  std::string ti50_emulator;
  std::optional<std::string> custom_keyboard_config;
  std::optional<std::string> domkey_mapping_config;
  int output_audio_streams_count = 1;
  std::optional<bool> enforce_mac80211_hwsim;
  int blank_data_image_mb = 0;
};

}
