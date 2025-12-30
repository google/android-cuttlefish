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
#include "cuttlefish/host/commands/assemble_cvd/guest_config.h"

#include <sys/types.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <fmt/format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

#include "cuttlefish/common/libs/key_equals_value/key_equals_value.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/common/libs/utils/in_sandbox.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/host/commands/assemble_cvd/boot_image_utils.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/boot_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/kernel_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/libs/config/config_utils.h"
#include "cuttlefish/host/libs/config/display.h"

#include "cuttlefish/host/commands/assemble_cvd/proto/guest_config.pb.h"

namespace cuttlefish {
namespace {

Result<void> ParseGuestConfigTextProto(const std::string& guest_config_path,
                                       GuestConfig& guest_config) {
  static const std::unordered_map<config::DeviceType, DeviceType>
      kDeviceTypeMap = {{config::DeviceType::Phone, DeviceType::Phone},
                        {config::DeviceType::Wear, DeviceType::Wear},
                        {config::DeviceType::Auto, DeviceType::Auto},
                        {config::DeviceType::Foldable, DeviceType::Foldable},
                        {config::DeviceType::Tv, DeviceType::Tv},
                        {config::DeviceType::Minidroid, DeviceType::Minidroid},
                        {config::DeviceType::Go, DeviceType::Go}};

  config::GuestConfigFile proto_config;

  const auto file_descriptor =
      open(guest_config_path.c_str(), O_RDONLY | O_CLOEXEC);
  CF_EXPECT(file_descriptor >= 0);

  google::protobuf::io::FileInputStream file_stream(file_descriptor);
  const auto result =
      google::protobuf::TextFormat::Parse(&file_stream, &proto_config);
  CF_EXPECT(close(file_descriptor) == 0);
  CF_EXPECT(result == true);

  guest_config.device_type = DeviceType::Unknown;
  if (proto_config.has_device_type()) {
    const auto it = kDeviceTypeMap.find(proto_config.device_type());
    if (it != kDeviceTypeMap.end()) {
      guest_config.device_type = it->second;
    }
  }

  const auto& graphics_config = proto_config.graphics();
  if (graphics_config.has_gfxstream_supported()) {
    guest_config.gfxstream_supported = graphics_config.gfxstream_supported();
  }
  if (graphics_config.has_gfxstream_gl_program_binary_link_status_supported()) {
    guest_config.gfxstream_gl_program_binary_link_status_supported =
        graphics_config.gfxstream_gl_program_binary_link_status_supported();
  }
  if (graphics_config.has_bgra_framebuffers_supported()) {
    guest_config.supports_bgra_framebuffers =
        graphics_config.bgra_framebuffers_supported();
  }
  if (graphics_config.has_prefer_drm_virgl_when_supported()) {
    guest_config.prefer_drm_virgl_when_supported =
        graphics_config.prefer_drm_virgl_when_supported();
  }

  const auto& input_config = proto_config.input();
  if (input_config.has_mouse_supported()) {
    guest_config.mouse_supported = input_config.mouse_supported();
  }
  if (input_config.has_gamepad_supported()) {
    guest_config.gamepad_supported = input_config.gamepad_supported();
  }
  if (input_config.has_custom_keyboard_config()) {
    guest_config.custom_keyboard_config =
        DefaultHostArtifactsPath(input_config.custom_keyboard_config());
  }
  if (input_config.has_domkey_mapping_config()) {
    guest_config.domkey_mapping_config =
        DefaultHostArtifactsPath(input_config.domkey_mapping_config());
  }

  if(proto_config.has_audio()) {
    guest_config.audio_settings = proto_config.audio();
  }

  const auto& network_config = proto_config.network();
  if (network_config.has_enforce_mac80211_hwsim()) {
    guest_config.enforce_mac80211_hwsim =
        network_config.enforce_mac80211_hwsim();
  }

  const auto& storage_config = proto_config.storage();
  if (storage_config.has_blank_data_image_mb()) {
    guest_config.blank_data_image_mb = storage_config.blank_data_image_mb();
  }

  const auto& virtualization_config = proto_config.virtualization();
  if (virtualization_config.has_vhost_user_vsock()) {
    guest_config.vhost_user_vsock = virtualization_config.vhost_user_vsock();
  }
  if (virtualization_config.has_ti50_emulator_path()) {
    guest_config.ti50_emulator = virtualization_config.ti50_emulator_path();
  }

  return {};
}

Result<std::string> GetAndroidInfoConfig(
    const std::string& android_info_file_path, const std::string& key) {
  CF_EXPECT(FileExists(android_info_file_path));

  std::string android_info_contents = ReadFile(android_info_file_path);
  auto android_info_map = CF_EXPECT(ParseKeyEqualsValue(android_info_contents));
  CF_EXPECT(android_info_map.find(key) != android_info_map.end());
  return android_info_map[key];
}

Result<void> ParseGuestConfigTxt(const std::string& guest_config_path,
                                 GuestConfig& guest_config) {
  auto res_device_type = GetAndroidInfoConfig(guest_config_path, "device_type");
  // If that "device_type" is not explicitly set, fall back to parse "config".
  if (!res_device_type.ok()) {
    res_device_type = GetAndroidInfoConfig(guest_config_path, "config");
  }
  guest_config.device_type = ParseDeviceType(res_device_type.value_or(""));

  auto res = GetAndroidInfoConfig(guest_config_path, "gfxstream");
  guest_config.gfxstream_supported = res.ok() && res.value() == "supported";

  res = GetAndroidInfoConfig(guest_config_path,
                             "gfxstream_gl_program_binary_link_status");
  guest_config.gfxstream_gl_program_binary_link_status_supported =
      res.ok() && res.value() == "supported";

  auto res_mouse_support = GetAndroidInfoConfig(guest_config_path, "mouse");
  guest_config.mouse_supported =
      res_mouse_support.ok() && res_mouse_support.value() == "supported";

  auto res_gamepad_support = GetAndroidInfoConfig(guest_config_path, "gamepad");
  guest_config.gamepad_supported =
      res_gamepad_support.ok() && res_gamepad_support.value() == "supported";

  auto res_custom_keyboard_config =
      GetAndroidInfoConfig(guest_config_path, "custom_keyboard");
  if (res_custom_keyboard_config.ok()) {
    guest_config.custom_keyboard_config =
        DefaultHostArtifactsPath(res_custom_keyboard_config.value());
  }

  auto res_domkey_mapping_config =
      GetAndroidInfoConfig(guest_config_path, "domkey_mapping");
  if (res_domkey_mapping_config.ok()) {
    guest_config.domkey_mapping_config =
        DefaultHostArtifactsPath(res_domkey_mapping_config.value());
  }

  auto res_bgra_support =
      GetAndroidInfoConfig(guest_config_path, "supports_bgra_framebuffers");
  guest_config.supports_bgra_framebuffers =
      res_bgra_support.value_or("") == "true";

  auto res_vhost_user_vsock =
      GetAndroidInfoConfig(guest_config_path, "vhost_user_vsock");
  guest_config.vhost_user_vsock = res_vhost_user_vsock.value_or("") == "true";

  auto res_prefer_drm_virgl_when_supported = GetAndroidInfoConfig(
      guest_config_path, "prefer_drm_virgl_when_supported");
  guest_config.prefer_drm_virgl_when_supported =
      res_prefer_drm_virgl_when_supported.value_or("") == "true";

  auto res_ti50_emulator =
      GetAndroidInfoConfig(guest_config_path, "ti50_emulator");
  guest_config.ti50_emulator = res_ti50_emulator.value_or("");
  auto res_output_audio_streams_count =
      GetAndroidInfoConfig(guest_config_path, "output_audio_streams_count");
  if (res_output_audio_streams_count.ok()) {
    std::string output_audio_streams_count_str =
        res_output_audio_streams_count.value();
    CF_EXPECT(android::base::ParseInt(output_audio_streams_count_str,
                                      &guest_config.output_audio_streams_count),
              "Failed to parse value \"" << output_audio_streams_count_str
                                         << "\" for output audio stream count");
  }

  Result<std::string> enforce_mac80211_hwsim =
      GetAndroidInfoConfig(guest_config_path, "enforce_mac80211_hwsim");
  if (enforce_mac80211_hwsim.ok()) {
    if (*enforce_mac80211_hwsim == "true") {
      guest_config.enforce_mac80211_hwsim = true;
    } else if (*enforce_mac80211_hwsim == "false") {
      guest_config.enforce_mac80211_hwsim = false;
    }
  }

  auto res_blank_data_image_mb =
      GetAndroidInfoConfig(guest_config_path, "blank_data_image_mb");
  if (res_blank_data_image_mb.ok()) {
    std::string res_blank_data_image_mb_str = res_blank_data_image_mb.value();
    CF_EXPECT(android::base::ParseInt(res_blank_data_image_mb_str,
                                      &guest_config.blank_data_image_mb),
              "Failed to parse value \"" << res_blank_data_image_mb_str
                                         << "\" for blank data image size");
  }

  return {};
}

}  // namespace

Result<std::vector<GuestConfig>> ReadGuestConfig(
    const BootImageFlag& boot_image, const KernelPathFlag& kernel_path,
    const SystemImageDirFlag& system_image_dir) {
  std::vector<GuestConfig> guest_configs;

  const std::string env_path = fmt::format(
      "PATH={}:{}", StringFromEnv("PATH", ""), DefaultHostArtifactsPath("bin"));
  for (int instance_index = 0; instance_index < system_image_dir.Size();
       instance_index++) {
    // extract-ikconfig can be called directly on the boot image since it looks
    // for the ikconfig header in the image before extracting the config list.
    // This code is liable to break if the boot image ever includes the
    // ikconfig header outside the kernel.
    std::string cur_boot_image = boot_image.ForIndex(instance_index);

    std::string kernel_image_path = "";
    if (!kernel_path.KernelPathForIndex(instance_index).empty()) {
      kernel_image_path = kernel_path.KernelPathForIndex(instance_index);
    } else if (!cur_boot_image.empty()) {
      kernel_image_path = cur_boot_image;
    }

    GuestConfig guest_config;
    guest_config.android_version_number =
        CF_EXPECT(ReadAndroidVersionFromBootImage(cur_boot_image),
                  "Failed to read guest's android version");

    if (InSandbox()) {
      // TODO: b/359309462 - real sandboxing for extract-ikconfig
      guest_config.target_arch = HostArch();
      guest_config.bootconfig_supported = true;
      guest_config.hctr2_supported = true;
    } else {
      Command ikconfig_cmd = Command(HostBinaryPath("extract-ikconfig"))
                                 .AddParameter(kernel_image_path)
                                 .UnsetFromEnvironment("PATH")
                                 .AddEnvironmentVariable("PATH", env_path);

      const std::string config =
          CF_EXPECT(RunAndCaptureStdout(std::move(ikconfig_cmd)));

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
      // Note there's also a platform dep for hctr2 introduced in Android 14.
      // Hence the version check.
      guest_config.hctr2_supported =
          (config.find("\nCONFIG_CRYPTO_HCTR2=y") != std::string::npos) &&
          (guest_config.android_version_number != "11.0.0") &&
          (guest_config.android_version_number != "13.0.0") &&
          (guest_config.android_version_number != "11") &&
          (guest_config.android_version_number != "13");
    }

    constexpr char kGuestConfigFilename[] = "cuttlefish-guest-config.txtpb";
    const std::string guest_config_path =
        system_image_dir.ForIndex(instance_index) + "/" + kGuestConfigFilename;
    if (FileExists(guest_config_path)) {
      CF_EXPECT(ParseGuestConfigTextProto(guest_config_path, guest_config));
    } else {
      const std::string android_info_txt_path =
          system_image_dir.ForIndex(instance_index) + "/android-info.txt";
      CF_EXPECT(ParseGuestConfigTxt(android_info_txt_path, guest_config));
    }

    guest_configs.push_back(guest_config);
  }
  return guest_configs;
}

}  // namespace cuttlefish
