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
#include <string_view>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "fmt/format.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"

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
#include "cuttlefish/host/commands/assemble_cvd/proto/guest_config.pb.h"
#include "cuttlefish/host/libs/config/config_utils.h"
#include "cuttlefish/host/libs/config/display.h"
#include "cuttlefish/pretty/optional.h"
#include "cuttlefish/pretty/string.h"

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

Result<std::string> MapGetResult(
    const std::map<std::string, std::string, std::less<void>> android_info,
    std::string_view key) {
  auto it = android_info.find(key);
  CF_EXPECT(it != android_info.end());
  return it->second;
}

bool MapHasValue(
    const std::map<std::string, std::string, std::less<void>> android_info,
    std::string_view key, std::string_view expected_value) {
  auto it = android_info.find(key);
  return it == android_info.end() ? false : it->second == expected_value;
}

Result<void> ParseGuestConfigTxt(const std::string& guest_config_path,
                                 GuestConfig& guest_config) {
  CF_EXPECT(FileExists(guest_config_path));
  const std::string android_info_contents = ReadFile(guest_config_path);
  const std::map<std::string, std::string, std::less<void>> info =
      CF_EXPECT(ParseKeyEqualsValue(android_info_contents));

  // If that "device_type" is not explicitly set, fall back to parse "config".
  guest_config.device_type =
      ParseDeviceType(MapGetResult(info, "device_type")
                          .value_or(MapGetResult(info, "config").value_or("")));

  guest_config.gfxstream_supported =
      MapHasValue(info, "gfxstream", "supported");

  guest_config.gfxstream_gl_program_binary_link_status_supported =
      MapHasValue(info, "gfxstream_gl_program_binary_link_status", "supported");

  guest_config.mouse_supported = MapHasValue(info, "mouse", "supported");

  guest_config.gamepad_supported = MapHasValue(info, "gamepad", "supported");

  if (const Result<std::string> res = MapGetResult(info, "custom_keyboard");
      res.ok()) {
    guest_config.custom_keyboard_config = DefaultHostArtifactsPath(*res);
  }

  if (const Result<std::string> res = MapGetResult(info, "domkey_mapping");
      res.ok()) {
    guest_config.domkey_mapping_config = DefaultHostArtifactsPath(*res);
  }

  guest_config.supports_bgra_framebuffers =
      MapHasValue(info, "supports_bgra_framebuffers", "true");

  guest_config.vhost_user_vsock = MapHasValue(info, "vhost_user_vsock", "true");

  guest_config.prefer_drm_virgl_when_supported =
      MapHasValue(info, "prefer_drm_virgl_when_supported", "true");

  guest_config.ti50_emulator = MapGetResult(info, "ti50_emulator").value_or("");

  if (const Result<std::string> res =
          MapGetResult(info, "output_audio_streams_count");
      res.ok()) {
    CF_EXPECTF(
        absl::SimpleAtoi(*res, &guest_config.output_audio_streams_count),
        "Failed to parse value '{}' for output audio stream count", *res);
  }

  if (const Result<std::string> res =
          MapGetResult(info, "enforce_mac80211_hwsim");
      res.ok()) {
    if (*res == "true") {
      guest_config.enforce_mac80211_hwsim = true;
    } else if (*res == "false") {
      guest_config.enforce_mac80211_hwsim = false;
    }
  }

  if (const Result<std::string> res = MapGetResult(info, "blank_data_image_mb");
      res.ok()) {
    CF_EXPECTF(absl::SimpleAtoi(*res, &guest_config.blank_data_image_mb),
               "Failed to parse value '{}' for blank data image size", *res);
  }

  return {};
}

}  // namespace

PrettyStruct Pretty(const GuestConfig& config, PrettyAdlPlaceholder) {
  return PrettyStruct("GuestConfig")
      .Member("target_arch", config.target_arch)
      .Member("device_type", config.device_type)
      .Member("bootconfig_supported", config.bootconfig_supported)
      .Member("hctr2_supported", config.hctr2_supported)
      .Member("android_version_number", config.android_version_number)
      .Member("gfxstream_supported", config.gfxstream_supported)
      .Member("gfxstream_gl_program_binary_link_status_supported",
              config.gfxstream_gl_program_binary_link_status_supported)
      .Member("vhost_user_vsock", config.vhost_user_vsock)
      .Member("supports_bgra_framebuffers", config.supports_bgra_framebuffers)
      .Member("prefer_drm_virgl_when_supported",
              config.prefer_drm_virgl_when_supported)
      .Member("mouse_supported", config.mouse_supported)
      .Member("gamepad_supported", config.gamepad_supported)
      .Member("ti50_emulator", config.ti50_emulator)
      .Member("custom_keyboard_config", config.custom_keyboard_config)
      .Member("domkey_mapping_config", config.domkey_mapping_config)
      .Member("output_audio_streams_count", config.output_audio_streams_count)
      .Member("audio_settings", config.audio_settings)
      .Member("enforce_mac80211_hwsim", config.enforce_mac80211_hwsim)
      .Member("blank_data_image_mb", config.blank_data_image_mb);
}

Result<std::vector<GuestConfig>> ReadGuestConfig(
    const BootImageFlag& boot_image, const KernelPathFlag& kernel_path,
    const SystemImageDirFlag& system_image_dirs) {
  std::vector<GuestConfig> guest_configs;

  const std::string env_path = fmt::format(
      "PATH={}:{}", StringFromEnv("PATH", ""), DefaultHostArtifactsPath("bin"));
  for (int instance_index = 0; instance_index < system_image_dirs.Size();
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

      if (absl::StrContains(config, "\nCONFIG_ARM=y")) {
        guest_config.target_arch = Arch::Arm;
      } else if (absl::StrContains(config, "\nCONFIG_ARM64=y")) {
        guest_config.target_arch = Arch::Arm64;
      } else if (absl::StrContains(config, "\nCONFIG_ARCH_RV64I=y")) {
        guest_config.target_arch = Arch::RiscV64;
      } else if (absl::StrContains(config, "\nCONFIG_X86_64=y")) {
        guest_config.target_arch = Arch::X86_64;
      } else if (absl::StrContains(config, "\nCONFIG_X86=y")) {
        guest_config.target_arch = Arch::X86;
      } else {
        return CF_ERR("Unknown target architecture");
      }
      guest_config.bootconfig_supported =
          absl::StrContains(config, "\nCONFIG_BOOT_CONFIG=y");
      // Once all Cuttlefish kernel versions are at least 5.15, this code can be
      // removed. CONFIG_CRYPTO_HCTR2=y will always be set.
      // Note there's also a platform dep for hctr2 introduced in Android 14.
      // Hence the version check.
      guest_config.hctr2_supported =
          (absl::StrContains(config, "\nCONFIG_CRYPTO_HCTR2=y")) &&
          (guest_config.android_version_number != "11.0.0") &&
          (guest_config.android_version_number != "13.0.0") &&
          (guest_config.android_version_number != "11") &&
          (guest_config.android_version_number != "13");
    }

    const std::string sys_img_dir = system_image_dirs.ForIndex(instance_index);

    constexpr char kGuestConfigFilename[] = "cuttlefish-guest-config.txtpb";

    const std::string guest_config_path =
        absl::StrCat(sys_img_dir, "/", kGuestConfigFilename);
    if (FileExists(guest_config_path)) {
      CF_EXPECT(ParseGuestConfigTextProto(guest_config_path, guest_config));
    } else {
      const std::string android_info_txt_path =
          absl::StrCat(sys_img_dir, "/android-info.txt");
      CF_EXPECT(ParseGuestConfigTxt(android_info_txt_path, guest_config));
    }

    guest_configs.push_back(std::move(guest_config));
  }
  return guest_configs;
}

}  // namespace cuttlefish
