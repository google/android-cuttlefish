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

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "cuttlefish/common/libs/utils/architecture.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/in_sandbox.h"
#include "cuttlefish/host/commands/assemble_cvd/assemble_cvd_flags.h"
#include "cuttlefish/host/commands/assemble_cvd/boot_image_utils.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/boot_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/kernel_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/misc_info.h"
#include "cuttlefish/host/libs/config/display.h"
#include "cuttlefish/host/libs/config/instance_nums.h"

namespace cuttlefish {
namespace {

Result<std::string> GetAndroidInfoConfig(
    const std::string& android_info_file_path, const std::string& key) {
  CF_EXPECT(FileExists(android_info_file_path));

  std::string android_info_contents = ReadFile(android_info_file_path);
  auto android_info_map = CF_EXPECT(ParseMiscInfo(android_info_contents));
  CF_EXPECT(android_info_map.find(key) != android_info_map.end());
  return android_info_map[key];
}

}  // namespace

Result<std::vector<GuestConfig>> ReadGuestConfig(
    const BootImageFlag& boot_image, const KernelPathFlag& kernel_path,
    const SystemImageDirFlag& system_image_dir) {
  std::vector<GuestConfig> guest_configs;
  std::string kernel_image_path = "";
  std::string cur_boot_image;

  std::string current_path = StringFromEnv("PATH", "");
  std::string bin_folder = DefaultHostArtifactsPath("bin");
  std::string new_path = "PATH=";
  new_path += current_path;
  new_path += ":";
  new_path += bin_folder;
  auto instance_nums =
      CF_EXPECT(InstanceNumsCalculator().FromGlobalGflags().Calculate());
  for (int instance_index = 0; instance_index < instance_nums.size();
       instance_index++) {
    // extract-ikconfig can be called directly on the boot image since it looks
    // for the ikconfig header in the image before extracting the config list.
    // This code is liable to break if the boot image ever includes the
    // ikconfig header outside the kernel.
    std::string cur_boot_image = boot_image.BootImageForIndex(instance_index);

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
      Command ikconfig_cmd(HostBinaryPath("extract-ikconfig"));
      ikconfig_cmd.AddParameter(kernel_image_path);
      ikconfig_cmd.UnsetFromEnvironment("PATH").AddEnvironmentVariable(
          "PATH", new_path);
      std::string ikconfig_path = FLAGS_early_tmp_dir + "/ikconfig.XXXXXX";
      auto ikconfig_fd = SharedFD::Mkstemp(&ikconfig_path);
      CF_EXPECT(ikconfig_fd->IsOpen(),
                "Unable to create ikconfig file: " << ikconfig_fd->StrError());
      ikconfig_cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut,
                                 ikconfig_fd);

      auto ikconfig_proc = ikconfig_cmd.Start();
      CF_EXPECT(ikconfig_proc.Started() && ikconfig_proc.Wait() == 0,
                "Failed to extract ikconfig from " << kernel_image_path);

      std::string config = ReadFile(ikconfig_path);

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

      unlink(ikconfig_path.c_str());
    }

    std::string instance_android_info_txt =
        system_image_dir.ForIndex(instance_index) + "/android-info.txt";

    auto res_device_type =
        GetAndroidInfoConfig(instance_android_info_txt, "device_type");
    // If that "device_type" is not explicitly set, fall back to parse "config".
    if (!res_device_type.ok()) {
      res_device_type =
          GetAndroidInfoConfig(instance_android_info_txt, "config");
    }
    guest_config.device_type = ParseDeviceType(res_device_type.value_or(""));

    auto res = GetAndroidInfoConfig(instance_android_info_txt, "gfxstream");
    guest_config.gfxstream_supported = res.ok() && res.value() == "supported";

    res = GetAndroidInfoConfig(instance_android_info_txt,
                               "gfxstream_gl_program_binary_link_status");
    guest_config.gfxstream_gl_program_binary_link_status_supported =
        res.ok() && res.value() == "supported";

    auto res_mouse_support =
        GetAndroidInfoConfig(instance_android_info_txt, "mouse");
    guest_config.mouse_supported =
        res_mouse_support.ok() && res_mouse_support.value() == "supported";
    
    auto res_gamepad_support =
        GetAndroidInfoConfig(instance_android_info_txt, "gamepad");
    guest_config.gamepad_supported =
        res_gamepad_support.ok() && res_gamepad_support.value() == "supported";

    auto res_custom_keyboard_config =
        GetAndroidInfoConfig(instance_android_info_txt, "custom_keyboard");
    if (res_custom_keyboard_config.ok()) {
      guest_config.custom_keyboard_config =
          DefaultHostArtifactsPath(res_custom_keyboard_config.value());
    }

    auto res_domkey_mapping_config =
        GetAndroidInfoConfig(instance_android_info_txt, "domkey_mapping");
    if (res_domkey_mapping_config.ok()) {
      guest_config.domkey_mapping_config =
          DefaultHostArtifactsPath(res_domkey_mapping_config.value());
    }

    auto res_bgra_support = GetAndroidInfoConfig(instance_android_info_txt,
                                                 "supports_bgra_framebuffers");
    guest_config.supports_bgra_framebuffers =
        res_bgra_support.value_or("") == "true";

    auto res_vhost_user_vsock =
        GetAndroidInfoConfig(instance_android_info_txt, "vhost_user_vsock");
    guest_config.vhost_user_vsock = res_vhost_user_vsock.value_or("") == "true";

    auto res_prefer_drm_virgl_when_supported = GetAndroidInfoConfig(
        instance_android_info_txt, "prefer_drm_virgl_when_supported");
    guest_config.prefer_drm_virgl_when_supported =
        res_prefer_drm_virgl_when_supported.value_or("") == "true";

    auto res_ti50_emulator =
        GetAndroidInfoConfig(instance_android_info_txt, "ti50_emulator");
    guest_config.ti50_emulator = res_ti50_emulator.value_or("");
    auto res_output_audio_streams_count = GetAndroidInfoConfig(
        instance_android_info_txt, "output_audio_streams_count");
    if (res_output_audio_streams_count.ok()) {
      std::string output_audio_streams_count_str =
          res_output_audio_streams_count.value();
      CF_EXPECT(
          android::base::ParseInt(output_audio_streams_count_str.c_str(),
                                  &guest_config.output_audio_streams_count),
          "Failed to parse value \"" << output_audio_streams_count_str
                                     << "\" for output audio stream count");
    }

    Result<std::string> enforce_mac80211_hwsim = GetAndroidInfoConfig(
        instance_android_info_txt, "enforce_mac80211_hwsim");
    if (enforce_mac80211_hwsim.ok()) {
      if (*enforce_mac80211_hwsim == "true") {
        guest_config.enforce_mac80211_hwsim = true;
      } else if (*enforce_mac80211_hwsim == "false") {
        guest_config.enforce_mac80211_hwsim = false;
      }
    }

    auto res_blank_data_image_mb =
        GetAndroidInfoConfig(instance_android_info_txt, "blank_data_image_mb");
    if (res_blank_data_image_mb.ok()) {
      std::string res_blank_data_image_mb_str = res_blank_data_image_mb.value();
      CF_EXPECT(android::base::ParseInt(res_blank_data_image_mb_str.c_str(),
                                        &guest_config.blank_data_image_mb),
                "Failed to parse value \"" << res_blank_data_image_mb_str
                                           << "\" for blank data image size");
    }

    guest_configs.push_back(guest_config);
  }
  return guest_configs;
}

}  // namespace cuttlefish
