/*crosvm
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

#include "host/libs/vm_manager/crosvm_manager.h"

#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cassert>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <json/json.h>
#include <vulkan/vulkan.h>

#include "common/libs/utils/architecture.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/known_paths.h"
#include "common/libs/utils/network.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/command_util/snapshot_utils.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"
#include "host/libs/vm_manager/crosvm_builder.h"
#include "host/libs/vm_manager/qemu_manager.h"
#include "host/libs/vm_manager/vhost_user.h"

namespace cuttlefish {
namespace vm_manager {

constexpr auto kTouchpadDefaultPrefix = "Crosvm_Virtio_Multitouch_Touchpad_";

bool CrosvmManager::IsSupported() {
#ifdef __ANDROID__
  return true;
#else
  return HostSupportsQemuCli();
#endif
}

Result<std::unordered_map<std::string, std::string>>
CrosvmManager::ConfigureGraphics(
    const CuttlefishConfig::InstanceSpecific& instance) {
  // Override the default HAL search paths in all cases. We do this because
  // the HAL search path allows for fallbacks, and fallbacks in conjunction
  // with properties lead to non-deterministic behavior while loading the
  // HALs.

  std::unordered_map<std::string, std::string> bootconfig_args;

  if (instance.gpu_mode() == kGpuModeGuestSwiftshader) {
    bootconfig_args = {
        {"androidboot.cpuvulkan.version", std::to_string(VK_API_VERSION_1_3)},
        {"androidboot.hardware.gralloc", "minigbm"},
        {"androidboot.hardware.hwcomposer", instance.hwcomposer()},
        {"androidboot.hardware.hwcomposer.display_finder_mode", "drm"},
        {"androidboot.hardware.hwcomposer.display_framebuffer_format",
         instance.guest_uses_bgra_framebuffers() ? "bgra" : "rgba"},
        {"androidboot.hardware.egl", "angle"},
        {"androidboot.hardware.vulkan", "pastel"},
        {"androidboot.opengles.version", "196609"},  // OpenGL ES 3.1
    };
  } else if (instance.gpu_mode() == kGpuModeDrmVirgl) {
    bootconfig_args = {
        {"androidboot.cpuvulkan.version", "0"},
        {"androidboot.hardware.gralloc", "minigbm"},
        {"androidboot.hardware.hwcomposer", "ranchu"},
        {"androidboot.hardware.hwcomposer.mode", "client"},
        {"androidboot.hardware.hwcomposer.display_finder_mode", "drm"},
        {"androidboot.hardware.hwcomposer.display_framebuffer_format",
         instance.guest_uses_bgra_framebuffers() ? "bgra" : "rgba"},
        {"androidboot.hardware.egl", "mesa"},
        // No "hardware" Vulkan support, yet
        {"androidboot.opengles.version", "196608"},  // OpenGL ES 3.0
    };
  } else if (instance.gpu_mode() == kGpuModeGfxstream ||
             instance.gpu_mode() == kGpuModeGfxstreamGuestAngle ||
             instance.gpu_mode() ==
                 kGpuModeGfxstreamGuestAngleHostSwiftShader ||
             instance.gpu_mode() == kGpuModeGfxstreamGuestAngleHostLavapipe) {
    const bool uses_angle =
        instance.gpu_mode() == kGpuModeGfxstreamGuestAngle ||
        instance.gpu_mode() == kGpuModeGfxstreamGuestAngleHostSwiftShader ||
        instance.gpu_mode() == kGpuModeGfxstreamGuestAngleHostLavapipe;

    const std::string gles_impl = uses_angle ? "angle" : "emulation";

    const std::string gfxstream_transport = instance.gpu_gfxstream_transport();
    CF_EXPECT(gfxstream_transport == "virtio-gpu-asg" ||
                  gfxstream_transport == "virtio-gpu-pipe",
              "Invalid Gfxstream transport option: \"" << gfxstream_transport
                                                       << "\"");

    bootconfig_args = {
        {"androidboot.cpuvulkan.version", "0"},
        {"androidboot.hardware.gralloc", "minigbm"},
        {"androidboot.hardware.hwcomposer", instance.hwcomposer()},
        {"androidboot.hardware.hwcomposer.display_finder_mode", "drm"},
        {"androidboot.hardware.hwcomposer.display_framebuffer_format",
         instance.guest_uses_bgra_framebuffers() ? "bgra" : "rgba"},
        {"androidboot.hardware.egl", gles_impl},
        {"androidboot.hardware.vulkan", "ranchu"},
        {"androidboot.hardware.gltransport", gfxstream_transport},
        {"androidboot.opengles.version", "196609"},  // OpenGL ES 3.1
    };
  } else if (instance.gpu_mode() == kGpuModeCustom) {
    bootconfig_args = {
        {"androidboot.cpuvulkan.version", "0"},
        {"androidboot.hardware.gralloc", "minigbm"},
        {"androidboot.hardware.hwcomposer", instance.hwcomposer()},
        {"androidboot.hardware.hwcomposer.display_finder_mode", "drm"},
        {"androidboot.hardware.hwcomposer.display_framebuffer_format",
         instance.guest_uses_bgra_framebuffers() ? "bgra" : "rgba"},
        {"androidboot.hardware.egl", "angle"},
        {"androidboot.hardware.vulkan", instance.guest_vulkan_driver()},
        {"androidboot.hardware.gltransport", "virtio-gpu-asg"},
        {"androidboot.opengles.version", "196609"},  // OpenGL ES 3.1
    };
  } else if (instance.gpu_mode() == kGpuModeNone) {
    return {};
  } else {
    return CF_ERR("Unknown GPU mode " << instance.gpu_mode());
  }

  if (auto r = instance.guest_hwui_renderer();
      r != GuestHwuiRenderer::kUnknown) {
    bootconfig_args["androidboot.hardware.guest_hwui_renderer"] = ToString(r);
  }

  const auto guest_renderer_preload = instance.guest_renderer_preload();
  if (guest_renderer_preload == GuestRendererPreload::kEnabled) {
    bootconfig_args["androidboot.hardware.guest_disable_renderer_preload"] =
        "false";
  } else if (guest_renderer_preload == GuestRendererPreload::kDisabled) {
    bootconfig_args["androidboot.hardware.guest_disable_renderer_preload"] =
        "true";
  }

  if (!instance.gpu_angle_feature_overrides_enabled().empty()) {
    bootconfig_args["androidboot.hardware.angle_feature_overrides_enabled"] =
        instance.gpu_angle_feature_overrides_enabled();
  }
  if (!instance.gpu_angle_feature_overrides_disabled().empty()) {
    bootconfig_args["androidboot.hardware.angle_feature_overrides_disabled"] =
        instance.gpu_angle_feature_overrides_disabled();
  }

  return bootconfig_args;
}

Result<std::unordered_map<std::string, std::string>>
CrosvmManager::ConfigureBootDevices(
    const CuttlefishConfig::InstanceSpecific& instance) {
  const int num_disks = instance.virtual_disk_paths().size();
  const bool has_gpu = instance.hwcomposer() != kHwComposerNone;
  // TODO There is no way to control this assignment with crosvm (yet)
  if (HostArch() == Arch::X86_64) {
    int num_gpu_pcis = has_gpu ? 1 : 0;
    if (instance.gpu_mode() != kGpuModeNone &&
        !instance.enable_gpu_vhost_user()) {
      // crosvm has an additional PCI device for an ISA bridge when running
      // with a gpu and without vhost user gpu.
      num_gpu_pcis += 1;
    }
    // virtio_gpu and virtio_wl precedes the first console or disk
    return ConfigureMultipleBootDevices("pci0000:00/0000:00:", 1 + num_gpu_pcis,
                                        num_disks);
  } else {
    // On ARM64 crosvm, block devices are on their own bridge, so we don't
    // need to calculate it, and the path is always the same
    return {{{"androidboot.boot_devices", "10000.pci"}}};
  }
}

std::string ToSingleLineString(const Json::Value& value) {
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString(builder, value);
}

Result<std::string> HostSwiftShaderIcdPathForArch() {
  switch (HostArch()) {
    case Arch::Arm64:
      return HostBinaryPath("aarch64-linux-gnu/vk_swiftshader_icd.json");
    case Arch::X86:
    case Arch::X86_64:
      return HostUsrSharePath("vulkan/icd.d/vk_swiftshader_icd.json");
    default:
      break;
  }
  return CF_ERR("Unhandled host arch " << HostArchStr()
                                       << " for finding SwiftShader ICD.");
}

Result<std::string> HostLavapipeIcdPathForArch() {
  switch (HostArch()) {
    case Arch::X86:
    case Arch::X86_64:
      return HostUsrSharePath("vulkan/icd.d/vk_lavapipe_icd.cf.json");
    default:
      break;
  }
  return CF_ERR("Unhandled host arch " << HostArchStr()
                                       << " for finding SwiftShader ICD.");
}

Result<void> MaybeConfigureVulkanIcd(const CuttlefishConfig& config,
                                     Command* command) {
  const auto& gpu_mode = config.ForDefaultInstance().gpu_mode();
  if (gpu_mode == kGpuModeGfxstreamGuestAngleHostSwiftShader) {
    const std::string swiftshader_icd_json_path =
        CF_EXPECT(HostSwiftShaderIcdPathForArch());

    // See https://github.com/KhronosGroup/Vulkan-Loader.
    command->AddEnvironmentVariable("VK_DRIVER_FILES",
                                    swiftshader_icd_json_path);
    command->AddEnvironmentVariable("VK_ICD_FILENAMES",
                                    swiftshader_icd_json_path);
  } else if (gpu_mode == kGpuModeGfxstreamGuestAngleHostLavapipe) {
    const std::string lavapipe_icd_json_path =
        CF_EXPECT(HostLavapipeIcdPathForArch());

    // See https://github.com/KhronosGroup/Vulkan-Loader.
    command->AddEnvironmentVariable("VK_DRIVER_FILES", lavapipe_icd_json_path);
    command->AddEnvironmentVariable("VK_ICD_FILENAMES", lavapipe_icd_json_path);
  }

  return {};
}

Result<std::string> CrosvmPathForVhostUserGpu(const CuttlefishConfig& config) {
  const auto& instance = config.ForDefaultInstance();
  switch (HostArch()) {
    case Arch::Arm64:
      return HostBinaryPath("aarch64-linux-gnu/crosvm");
    case Arch::X86:
    case Arch::X86_64:
      return instance.crosvm_binary();
    default:
      break;
  }
  return CF_ERR("Unhandled host arch " << HostArchStr()
                                       << " for vhost user gpu crosvm");
}

Result<VhostUserDeviceCommands> BuildVhostUserGpu(
    const CuttlefishConfig& config, Command* main_crosvm_cmd) {
  const auto& instance = config.ForDefaultInstance();
  if (!instance.enable_gpu_vhost_user()) {
    return CF_ERR("Attempting to build vhost user gpu when not enabled?");
  }

  auto gpu_device_socket_path =
      instance.PerInstanceInternalUdsPath("vhost-user-gpu-socket");
  auto gpu_device_logs_path =
      instance.PerInstanceInternalPath("crosvm_vhost_user_gpu.fifo");
  auto gpu_device_logs = CF_EXPECT(SharedFD::Fifo(gpu_device_logs_path, 0666));

  Command gpu_device_logs_cmd(HostBinaryPath("log_tee"));
  gpu_device_logs_cmd.AddParameter("--process_name=crosvm_gpu");
  gpu_device_logs_cmd.AddParameter("--log_fd_in=", gpu_device_logs);
  gpu_device_logs_cmd.SetStopper(KillSubprocessFallback([](Subprocess* proc) {
    // Ask nicely so that log_tee gets a chance to process all the logs.
    // TODO: b/335934714 - Make sure the process actually exits
    bool res = kill(proc->pid(), SIGINT) == 0;
    return res ? StopperResult::kStopSuccess : StopperResult::kStopFailure;
  }));

  const std::string crosvm_path = CF_EXPECT(CrosvmPathForVhostUserGpu(config));

  CrosvmBuilder gpu_device_cmd;

  // NOTE: The "main" crosvm process returns a kCrosvmVmResetExitCode when the
  // guest exits but the "gpu" crosvm just exits cleanly with 0 after the "main"
  // crosvm disconnects.
  gpu_device_cmd.ApplyProcessRestarter(crosvm_path,
                                       /*first_time_argument=*/"",
                                       /*exit_code=*/0);

  gpu_device_cmd.Cmd().AddParameter("device");
  gpu_device_cmd.Cmd().AddParameter("gpu");

  const auto& gpu_mode = instance.gpu_mode();
  CF_EXPECT(
      gpu_mode == kGpuModeGfxstream ||
          gpu_mode == kGpuModeGfxstreamGuestAngle ||
          gpu_mode == kGpuModeGfxstreamGuestAngleHostSwiftShader ||
          gpu_mode == kGpuModeGfxstreamGuestAngleHostLavapipe,
      "GPU mode " << gpu_mode << " not yet supported with vhost user gpu.");

  const std::string gpu_pci_address =
      fmt::format("00:{:0>2x}.0", VmManager::kGpuPciSlotNum);

  // Why does this need JSON instead of just following the normal flags style...
  Json::Value gpu_params_json;
  gpu_params_json["pci-address"] = gpu_pci_address;
  if (gpu_mode == kGpuModeGfxstream) {
    gpu_params_json["context-types"] = "gfxstream-gles:gfxstream-vulkan";
    gpu_params_json["egl"] = true;
    gpu_params_json["gles"] = true;
  } else if (gpu_mode == kGpuModeGfxstreamGuestAngle ||
             gpu_mode == kGpuModeGfxstreamGuestAngleHostSwiftShader ||
             gpu_mode == kGpuModeGfxstreamGuestAngleHostLavapipe) {
    gpu_params_json["context-types"] = "gfxstream-vulkan";
    gpu_params_json["egl"] = false;
    gpu_params_json["gles"] = false;
  }
  gpu_params_json["glx"] = false;
  gpu_params_json["surfaceless"] = true;
  gpu_params_json["external-blob"] = instance.enable_gpu_external_blob();
  gpu_params_json["system-blob"] = instance.enable_gpu_system_blob();
  if (!instance.gpu_renderer_features().empty()) {
    gpu_params_json["renderer-features"] = instance.gpu_renderer_features();
  }

  if (instance.hwcomposer() != kHwComposerNone) {
    // "displays": [
    //   {
    //    "mode": {
    //      "windowed": [
    //        720,
    //        1280
    //      ]
    //    },
    //    "dpi": [
    //      320,
    //      320
    //    ],
    //    "refresh-rate": 60
    //   }
    // ]
    Json::Value displays(Json::arrayValue);
    for (const auto& display_config : instance.display_configs()) {
      Json::Value display_mode_windowed(Json::arrayValue);
      display_mode_windowed[0] = display_config.width;
      display_mode_windowed[1] = display_config.height;

      Json::Value display_mode;
      display_mode["windowed"] = display_mode_windowed;

      Json::Value display_dpi(Json::arrayValue);
      display_dpi[0] = display_config.dpi;
      display_dpi[1] = display_config.dpi;

      Json::Value display;
      display["mode"] = display_mode;
      display["dpi"] = display_dpi;
      display["refresh-rate"] = display_config.refresh_rate_hz;

      displays.append(display);
    }
    gpu_params_json["displays"] = displays;

    gpu_device_cmd.Cmd().AddParameter("--wayland-sock=",
                                      instance.frames_socket_path());
  }

  // Connect device to main crosvm:
  gpu_device_cmd.Cmd().AddParameter("--socket=", gpu_device_socket_path);

  main_crosvm_cmd->AddParameter(
      "--vhost-user=gpu,pci-address=", gpu_pci_address,
      ",socket=", gpu_device_socket_path);

  gpu_device_cmd.Cmd().AddParameter("--params");
  gpu_device_cmd.Cmd().AddParameter(ToSingleLineString(gpu_params_json));

  CF_EXPECT(MaybeConfigureVulkanIcd(config, &gpu_device_cmd.Cmd()));

  gpu_device_cmd.Cmd().RedirectStdIO(Subprocess::StdIOChannel::kStdOut,
                                     gpu_device_logs);
  gpu_device_cmd.Cmd().RedirectStdIO(Subprocess::StdIOChannel::kStdErr,
                                     gpu_device_logs);

  return VhostUserDeviceCommands{
      .device_cmd = std::move(gpu_device_cmd.Cmd()),
      .device_logs_cmd = std::move(gpu_device_logs_cmd),
      .socket_path = gpu_device_socket_path,
  };
}

Result<void> ConfigureGpu(const CuttlefishConfig& config, Command* crosvm_cmd) {
  const auto& instance = config.ForDefaultInstance();
  const auto& gpu_mode = instance.gpu_mode();

  const std::string gles_string =
      gpu_mode == kGpuModeGfxstreamGuestAngle ||
              gpu_mode == kGpuModeGfxstreamGuestAngleHostSwiftShader ||
              gpu_mode == kGpuModeGfxstreamGuestAngleHostLavapipe
          ? ",gles=false"
          : ",gles=true";

  // 256MB so it is small enough for a 32-bit kernel.
  const bool target_is_32bit = instance.target_arch() == Arch::Arm ||
                               instance.target_arch() == Arch::X86;
  const std::string gpu_pci_bar_size =
      target_is_32bit ? ",pci-bar-size=268435456" : "";

  const std::string gpu_udmabuf_string =
      instance.enable_gpu_udmabuf() ? ",udmabuf=true" : "";

  const std::string gpu_renderer_features = instance.gpu_renderer_features();
  const std::string gpu_renderer_features_param =
      !gpu_renderer_features.empty()
          ? ",renderer-features=\"" + gpu_renderer_features + "\""
          : "";

  const std::string gpu_common_string =
      fmt::format(",pci-address=00:{:0>2x}.0", VmManager::kGpuPciSlotNum) +
      gpu_udmabuf_string + gpu_pci_bar_size;
  const std::string gpu_common_3d_string =
      gpu_common_string + ",egl=true,surfaceless=true,glx=false" + gles_string +
      gpu_renderer_features_param;

  std::string gpu_displays_string = "";
  if (instance.hwcomposer() != kHwComposerNone) {
    std::vector<std::string> gpu_displays_strings;
    for (const auto& display_config : instance.display_configs()) {
      const auto display_w = std::to_string(display_config.width);
      const auto display_h = std::to_string(display_config.height);
      const auto display_dpi = std::to_string(display_config.dpi);
      const auto display_rr = std::to_string(display_config.refresh_rate_hz);
      gpu_displays_strings.push_back(android::base::Join(
          std::vector<std::string>{
              "mode=windowed[" + display_w + "," + display_h + "]",
              "dpi=[" + display_dpi + "," + display_dpi + "]",
              "refresh-rate=" + display_rr,
          },
          ","));
    }
    gpu_displays_string = "displays=[[" +
                          android::base::Join(gpu_displays_strings, "],[") +
                          "]],";

    crosvm_cmd->AddParameter("--wayland-sock=", instance.frames_socket_path());
  }

  if (gpu_mode == kGpuModeGuestSwiftshader) {
    crosvm_cmd->AddParameter("--gpu=", gpu_displays_string, "backend=2D",
                             gpu_common_string);
  } else if (gpu_mode == kGpuModeDrmVirgl) {
    crosvm_cmd->AddParameter("--gpu=", gpu_displays_string,
                             "backend=virglrenderer,context-types=virgl2",
                             gpu_common_3d_string);
  } else if (gpu_mode == kGpuModeGfxstream) {
    crosvm_cmd->AddParameter(
        "--gpu=", gpu_displays_string,
        "context-types=gfxstream-gles:gfxstream-vulkan:gfxstream-composer",
        gpu_common_3d_string);
  } else if (gpu_mode == kGpuModeGfxstreamGuestAngle ||
             gpu_mode == kGpuModeGfxstreamGuestAngleHostSwiftShader ||
             gpu_mode == kGpuModeGfxstreamGuestAngleHostLavapipe) {
    crosvm_cmd->AddParameter(
        "--gpu=", gpu_displays_string,
        "context-types=gfxstream-vulkan:gfxstream-composer",
        gpu_common_3d_string);
  } else if (gpu_mode == kGpuModeCustom) {
    crosvm_cmd->AddParameter("--gpu=", gpu_displays_string,
                             "context-types=" + instance.gpu_context_types(),
                             gpu_common_string);
  }

  CF_EXPECT(MaybeConfigureVulkanIcd(config, crosvm_cmd));

  return {};
}

Result<std::vector<MonitorCommand>> CrosvmManager::StartCommands(
    const CuttlefishConfig& config,
    std::vector<VmmDependencyCommand*>& dependencyCommands) {
  auto instance = config.ForDefaultInstance();
  auto environment = config.ForDefaultEnvironment();

  std::vector<MonitorCommand> commands;

  CrosvmBuilder crosvm_cmd;
  crosvm_cmd.Cmd().AddPrerequisite([&dependencyCommands]() -> Result<void> {
    for (auto dependencyCommand : dependencyCommands) {
      CF_EXPECT(dependencyCommand->WaitForAvailability());
    }

    return {};
  });

  // Add "--restore_path=<guest snapshot directory>" if there is a snapshot
  // path supplied.
  //
  // Use the process_restarter "-first_time_argument" flag to only do this for
  // the first invocation. If the guest requests a restart, we don't want crosvm
  // to restore again. It should reboot normally.
  std::string first_time_argument;
  if (IsRestoring(config)) {
    const std::string snapshot_dir_path = config.snapshot_path();
    auto meta_info_json = CF_EXPECT(LoadMetaJson(snapshot_dir_path));
    const std::vector<std::string> selectors{kGuestSnapshotField,
                                             instance.id()};
    const auto guest_snapshot_dir_suffix =
        CF_EXPECT(GetValue<std::string>(meta_info_json, selectors));
    // guest_snapshot_dir_suffix is a relative to
    // the snapshot_path
    const auto restore_path = snapshot_dir_path + "/" +
                              guest_snapshot_dir_suffix + "/" +
                              kGuestSnapshotBase;
    first_time_argument = "--restore=" + restore_path;
  }

  crosvm_cmd.ApplyProcessRestarter(instance.crosvm_binary(),
                                   first_time_argument, kCrosvmVmResetExitCode);
  crosvm_cmd.Cmd().AddParameter("run");
  crosvm_cmd.AddControlSocket(instance.CrosvmSocketPath(),
                              instance.crosvm_binary());

  if (!config.kvm_path().empty()) {
    crosvm_cmd.AddKvmPath(config.kvm_path());
  }

  if (!instance.smt()) {
    crosvm_cmd.Cmd().AddParameter("--no-smt");
  }

  // Disable USB passthrough. It isn't needed for any key use cases and it is
  // not compatible with crosvm suspend-resume support yet (b/266622743).
  // TODO: Allow it to be turned back on using a flag.
  if (!instance.enable_usb()) {
    crosvm_cmd.Cmd().AddParameter("--no-usb");
  }

  crosvm_cmd.Cmd().AddParameter("--core-scheduling=false");

  crosvm_cmd.Cmd().AddParameter("--vhost-user-connect-timeout-ms=", 30 * 1000);

  if (instance.vhost_net()) {
    crosvm_cmd.Cmd().AddParameter("--vhost-net");
  }

  if (config.virtio_mac80211_hwsim() &&
      !environment.vhost_user_mac80211_hwsim().empty()) {
    crosvm_cmd.Cmd().AddParameter("--vhost-user=mac80211-hwsim,socket=",
                                  environment.vhost_user_mac80211_hwsim());
  }

  if (instance.protected_vm()) {
    crosvm_cmd.Cmd().AddParameter("--protected-vm");
  }

  if (!instance.crosvm_use_balloon()) {
    crosvm_cmd.Cmd().AddParameter("--no-balloon");
  }

  if (!instance.crosvm_use_rng()) {
    crosvm_cmd.Cmd().AddParameter("--no-rng");
  }

  if (instance.gdb_port() > 0) {
    CF_EXPECT(instance.cpus() == 1, "CPUs must be 1 for crosvm gdb mode");
    crosvm_cmd.Cmd().AddParameter("--gdb=", instance.gdb_port());
  }

  std::optional<VhostUserDeviceCommands> vhost_user_gpu;
  if (instance.enable_gpu_vhost_user()) {
    vhost_user_gpu.emplace(
        CF_EXPECT(BuildVhostUserGpu(config, &crosvm_cmd.Cmd())));
  } else {
    CF_EXPECT(ConfigureGpu(config, &crosvm_cmd.Cmd()));
  }

  if (instance.hwcomposer() != kHwComposerNone) {
    const bool pmem_disabled = instance.mte() || !instance.use_pmem();
    if (!pmem_disabled && FileExists(instance.hwcomposer_pmem_path())) {
      crosvm_cmd.Cmd().AddParameter("--pmem=path=",
                                    instance.hwcomposer_pmem_path());
    }
  }

  const auto gpu_capture_enabled = !instance.gpu_capture_binary().empty();

  // crosvm_cmd.Cmd().AddParameter("--null-audio");
  crosvm_cmd.Cmd().AddParameter("--mem=", instance.memory_mb());
  if (instance.mte()) {
    crosvm_cmd.Cmd().AddParameter("--mte");
  }

  CF_EXPECT(crosvm_cmd.AddCpus(instance.cpus(), instance.vcpu_config_path()));

  auto disk_num = instance.virtual_disk_paths().size();
  CF_EXPECT(VmManager::kMaxDisks >= disk_num,
            "Provided too many disks (" << disk_num << "), maximum "
                                        << VmManager::kMaxDisks << "supported");
  size_t disk_i = 0;
  for (const auto& disk : instance.virtual_disk_paths()) {
    if (instance.protected_vm()) {
      crosvm_cmd.AddReadOnlyDisk(disk);
    } else if (instance.vhost_user_block() && disk_i == 2) {
      // TODO: b/346855591 - Run on all devices
      auto block = CF_EXPECT(VhostUserBlockDevice(config, disk_i, disk));
      commands.emplace_back(std::move(block.device_cmd));
      commands.emplace_back(std::move(block.device_logs_cmd));
      auto socket_path = std::move(block.socket_path);
      crosvm_cmd.Cmd().AddPrerequisite([socket_path]() -> Result<void> {
#ifdef __linux__
        return WaitForUnixSocketListeningWithoutConnect(socket_path,
                                                        /*timeoutSec=*/30);
#else
        return CF_ERR("Unhandled check if vhost user block ready.");
#endif
      });
      auto pci_addr = fmt::format("00:{:0>2x}.0", 0x13 + disk_i);
      crosvm_cmd.Cmd().AddParameter("--vhost-user=block,socket=", socket_path,
                                    ",pci-address=", pci_addr);
    } else {
      crosvm_cmd.AddReadWriteDisk(disk);
    }
    disk_i++;
  }

  if (instance.enable_webrtc()) {
    auto display_configs = instance.display_configs();
    CF_EXPECT(display_configs.size() >= 1);

    const int display_cnt = instance.display_configs().size();
    const int touchpad_cnt = instance.touchpad_configs().size();
    const int total_touch_cnt = display_cnt + touchpad_cnt;
    for (int touch_idx = 0; touch_idx < total_touch_cnt; ++touch_idx) {
      crosvm_cmd.AddVhostUser("input", instance.touch_socket_path(touch_idx));
    }
    if (instance.enable_mouse()) {
      crosvm_cmd.AddVhostUser("input", instance.mouse_socket_path());
    }
    crosvm_cmd.AddVhostUser("input", instance.rotary_socket_path());
    crosvm_cmd.AddVhostUser("input", instance.keyboard_socket_path());
    crosvm_cmd.AddVhostUser("input", instance.switches_socket_path());
  }

  // GPU capture can only support named files and not file descriptors due to
  // having to pass arguments to crosvm via a wrapper script.
#ifdef __linux__
  if (instance.enable_tap_devices() && !gpu_capture_enabled) {
    // The PCI ordering of tap devices is important. Make sure any change here
    // is reflected in ethprime u-boot variable.
    // TODO(b/218364216, b/322862402): Crosvm occupies 32 PCI devices first and
    // only then uses PCI functions which may break order. The final solution is
    // going to be a PCI allocation strategy that will guarantee the ordering.
    // For now, hardcode PCI network devices to unoccupied functions.
    const pci::Address mobile_pci =
        pci::Address(0, VmManager::kNetPciDeviceNum, 1);
    const pci::Address ethernet_pci =
        pci::Address(0, VmManager::kNetPciDeviceNum, 2);
    crosvm_cmd.AddTap(instance.mobile_tap_name(), instance.mobile_mac(),
                      mobile_pci);
    crosvm_cmd.AddTap(instance.ethernet_tap_name(), instance.ethernet_mac(),
                      ethernet_pci);

    if (!config.virtio_mac80211_hwsim() && environment.enable_wifi()) {
      crosvm_cmd.AddTap(instance.wifi_tap_name());
    }
  }
#endif

  const bool pmem_disabled = instance.mte() || !instance.use_pmem();
  if (!pmem_disabled && FileExists(instance.access_kregistry_path())) {
    crosvm_cmd.Cmd().AddParameter("--pmem=path=",
                                  instance.access_kregistry_path());
  }

  if (!pmem_disabled && FileExists(instance.pstore_path())) {
    crosvm_cmd.Cmd().AddParameter("--pstore=path=", instance.pstore_path(),
                                  ",size=", FileSize(instance.pstore_path()));
  }

  if (instance.enable_sandbox()) {
    const bool seccomp_exists = DirectoryExists(instance.seccomp_policy_dir());
    const std::string& var_empty_dir = kCrosvmVarEmptyDir;
    const bool var_empty_available = DirectoryExists(var_empty_dir);
    CF_EXPECT(var_empty_available && seccomp_exists,
              var_empty_dir << " is not an existing, empty directory."
                            << "seccomp-policy-dir, "
                            << instance.seccomp_policy_dir()
                            << " does not exist");
    crosvm_cmd.Cmd().AddParameter("--seccomp-policy-dir=",
                                  instance.seccomp_policy_dir());
  } else {
    crosvm_cmd.Cmd().AddParameter("--disable-sandbox");
  }

  if (instance.vsock_guest_cid() >= 2) {
    if (instance.vhost_user_vsock()) {
      crosvm_cmd.AddVhostUser(
          "vsock", fmt::format("{}/vsock_{}_{}/vhost.socket", TempDir(),
                               instance.vsock_guest_cid(), getuid()));
    } else {
      crosvm_cmd.Cmd().AddParameter("--cid=", instance.vsock_guest_cid());
    }
  }

  // /dev/hvc0 = kernel console
  // If kernel log is enabled, the virtio-console port will be specified as
  // a true console for Linux, and kernel messages will be printed there.
  // Otherwise, the port will still be set up for bootloader and userspace
  // messages, but the kernel will not print anything here. This keeps our
  // kernel log event features working. If an alternative "earlycon" boot
  // console is configured below on a legacy serial port, it will control
  // the main log until the virtio-console takes over.
  crosvm_cmd.AddHvcReadOnly(instance.kernel_log_pipe_name(),
                            instance.enable_kernel_log());

  // /dev/hvc1 = serial console
  if (instance.console()) {
    // stdin is the only currently supported way to write data to a serial port
    // in crosvm. A file (named pipe) is used here instead of stdout to ensure
    // only the serial port output is received by the console forwarder as
    // crosvm may print other messages to stdout.
    if (instance.kgdb() || instance.use_bootloader()) {
      crosvm_cmd.AddSerialConsoleReadWrite(instance.console_out_pipe_name(),
                                           instance.console_in_pipe_name(),
                                           instance.enable_kernel_log());
      // In kgdb mode, we have the interactive console on ttyS0 (both Android's
      // console and kdb), so we can disable the virtio-console port usually
      // allocated to Android's serial console, and redirect it to a sink. This
      // ensures that that the PCI device assignments (and thus sepolicy) don't
      // have to change
      crosvm_cmd.AddHvcSink();
    } else {
      crosvm_cmd.AddSerialSink();
      crosvm_cmd.AddHvcReadWrite(instance.console_out_pipe_name(),
                                 instance.console_in_pipe_name());
    }
  } else {
    // Use an 8250 UART (ISA or platform device) for earlycon, as the
    // virtio-console driver may not be available for early messages
    // In kgdb mode, earlycon is an interactive console, and so early
    // dmesg will go there instead of the kernel.log
    if (instance.enable_kernel_log() &&
        (instance.kgdb() || instance.use_bootloader())) {
      crosvm_cmd.AddSerialConsoleReadOnly(instance.kernel_log_pipe_name());
    }

    // as above, create a fake virtio-console 'sink' port when the serial
    // console is disabled, so the PCI device ID assignments don't move
    // around
    crosvm_cmd.AddHvcSink();
  }

  auto crosvm_logs_path = instance.PerInstanceInternalPath("crosvm.fifo");
  auto crosvm_logs = CF_EXPECT(SharedFD::Fifo(crosvm_logs_path, 0666));

  Command crosvm_log_tee_cmd(HostBinaryPath("log_tee"));
  crosvm_log_tee_cmd.AddParameter("--process_name=crosvm");
  crosvm_log_tee_cmd.AddParameter("--log_fd_in=", crosvm_logs);
  crosvm_log_tee_cmd.SetStopper(KillSubprocessFallback([](Subprocess* proc) {
    // Ask nicely so that log_tee gets a chance to process all the logs.
    bool res = kill(proc->pid(), SIGINT) == 0;
    // TODO: b/335934714 - Make sure the process actually exits
    return res ? StopperResult::kStopSuccess : StopperResult::kStopFailure;
  }));

  // /dev/hvc2 = serial logging
  // Serial port for logcat, redirected to a pipe
  crosvm_cmd.AddHvcReadOnly(instance.logcat_pipe_name());

  // /dev/hvc3 = keymaster (C++ implementation)
  crosvm_cmd.AddHvcReadWrite(
      instance.PerInstanceInternalPath("keymaster_fifo_vm.out"),
      instance.PerInstanceInternalPath("keymaster_fifo_vm.in"));
  // /dev/hvc4 = gatekeeper
  crosvm_cmd.AddHvcReadWrite(
      instance.PerInstanceInternalPath("gatekeeper_fifo_vm.out"),
      instance.PerInstanceInternalPath("gatekeeper_fifo_vm.in"));

  // /dev/hvc5 = bt
  if (config.enable_host_bluetooth()) {
    crosvm_cmd.AddHvcReadWrite(
        instance.PerInstanceInternalPath("bt_fifo_vm.out"),
        instance.PerInstanceInternalPath("bt_fifo_vm.in"));
  } else {
    crosvm_cmd.AddHvcSink();
  }

  // /dev/hvc6 = gnss
  // /dev/hvc7 = location
  if (instance.enable_gnss_grpc_proxy()) {
    crosvm_cmd.AddHvcReadWrite(
        instance.PerInstanceInternalPath("gnsshvc_fifo_vm.out"),
        instance.PerInstanceInternalPath("gnsshvc_fifo_vm.in"));
    crosvm_cmd.AddHvcReadWrite(
        instance.PerInstanceInternalPath("locationhvc_fifo_vm.out"),
        instance.PerInstanceInternalPath("locationhvc_fifo_vm.in"));
  } else {
    for (auto i = 0; i < 2; i++) {
      crosvm_cmd.AddHvcSink();
    }
  }

  // /dev/hvc8 = confirmationui
  crosvm_cmd.AddHvcReadWrite(
      instance.PerInstanceInternalPath("confui_fifo_vm.out"),
      instance.PerInstanceInternalPath("confui_fifo_vm.in"));

  // /dev/hvc9 = uwb
  if (config.enable_host_uwb()) {
    crosvm_cmd.AddHvcReadWrite(
        instance.PerInstanceInternalPath("uwb_fifo_vm.out"),
        instance.PerInstanceInternalPath("uwb_fifo_vm.in"));
  } else {
    crosvm_cmd.AddHvcSink();
  }

  // /dev/hvc10 = oemlock
  crosvm_cmd.AddHvcReadWrite(
      instance.PerInstanceInternalPath("oemlock_fifo_vm.out"),
      instance.PerInstanceInternalPath("oemlock_fifo_vm.in"));

  // /dev/hvc11 = keymint (Rust implementation)
  crosvm_cmd.AddHvcReadWrite(
      instance.PerInstanceInternalPath("keymint_fifo_vm.out"),
      instance.PerInstanceInternalPath("keymint_fifo_vm.in"));

  // /dev/hvc12 = NFC
  if (config.enable_host_nfc()) {
    crosvm_cmd.AddHvcReadWrite(
        instance.PerInstanceInternalPath("nfc_fifo_vm.out"),
        instance.PerInstanceInternalPath("nfc_fifo_vm.in"));
  } else {
    crosvm_cmd.AddHvcSink();
  }

  // /dev/hvc13 = sensors
  crosvm_cmd.AddHvcReadWrite(
      instance.PerInstanceInternalPath("sensors_fifo_vm.out"),
      instance.PerInstanceInternalPath("sensors_fifo_vm.in"));

  // /dev/hvc14 = MCU CONTROL
  if (instance.mcu()["control"]["type"].asString() == "serial") {
    auto path = instance.PerInstanceInternalPath("mcu");
    path += "/" + instance.mcu()["control"]["path"].asString();
    crosvm_cmd.AddHvcReadWrite(path, path);
  } else {
    crosvm_cmd.AddHvcSink();
  }

  // /dev/hvc15 = MCU UART
  if (instance.mcu()["uart0"]["type"].asString() == "serial") {
    auto path = instance.PerInstanceInternalPath("mcu");
    path += "/" + instance.mcu()["uart0"]["path"].asString();
    crosvm_cmd.AddHvcReadWrite(path, path);
  } else {
    crosvm_cmd.AddHvcSink();
  }

  // /dev/hvc16 = Ti50 TPM FIFO
  if (!instance.ti50_emulator().empty()) {
    crosvm_cmd.AddHvcSocket(instance.PerInstancePath("direct_tpm_fifo"));
  } else {
    crosvm_cmd.AddHvcSink();
  }

  for (auto i = 0; i < VmManager::kMaxDisks - disk_num; i++) {
    crosvm_cmd.AddHvcSink();
  }
  CF_EXPECT(crosvm_cmd.HvcNum() + disk_num ==
                VmManager::kMaxDisks + VmManager::kDefaultNumHvcs,
            "HVC count (" << crosvm_cmd.HvcNum() << ") + disk count ("
                          << disk_num << ") is not the expected total of "
                          << VmManager::kMaxDisks + VmManager::kDefaultNumHvcs
                          << " devices");

  if (instance.enable_audio()) {
    crosvm_cmd.Cmd().AddParameter("--sound=", instance.audio_server_path());
  }

  // TODO(b/162071003): virtiofs crashes without sandboxing, this should be
  // fixed
  if (instance.enable_virtiofs()) {
    CF_EXPECT(instance.enable_sandbox(),
              "virtiofs is currently not supported without sandboxing");
    // Set up directory shared with virtiofs, setting security_ctx option to
    // false prevents host error when unable to write data in the
    // /proc/thread-self/attr/fscreate file.
    crosvm_cmd.Cmd().AddParameter(
        "--shared-dir=", instance.PerInstancePath(kSharedDirName),
        ":shared:type=fs:security_ctx=false");
  }

  if (instance.target_arch() == Arch::X86_64) {
    crosvm_cmd.Cmd().AddParameter("--pflash=", instance.pflash_path());
  }

  // This needs to be the last parameter
  crosvm_cmd.Cmd().AddParameter("--bios=", instance.bootloader());

  if (vhost_user_gpu) {
    // The vhost user gpu crosvm command should be added before the main
    // crosvm command so that the main crosvm command can use a prerequisite
    // to wait for the communication socket to be ready.
    commands.emplace_back(std::move(vhost_user_gpu->device_cmd));
    commands.emplace_back(std::move(vhost_user_gpu->device_logs_cmd));
  }

  // log_tee must be added before crosvm_cmd to ensure all of crosvm's logs are
  // captured during shutdown. Processes are stopped in reverse order.
  commands.emplace_back(std::move(crosvm_log_tee_cmd));

  if (gpu_capture_enabled) {
    const std::string gpu_capture_basename =
        android::base::Basename(instance.gpu_capture_binary());

    auto gpu_capture_logs_path =
        instance.PerInstanceInternalPath("gpu_capture.fifo");
    auto gpu_capture_logs =
        CF_EXPECT(SharedFD::Fifo(gpu_capture_logs_path, 0666));

    Command gpu_capture_log_tee_cmd(HostBinaryPath("log_tee"));
    gpu_capture_log_tee_cmd.AddParameter("--process_name=",
                                         gpu_capture_basename);
    gpu_capture_log_tee_cmd.AddParameter("--log_fd_in=", gpu_capture_logs);

    Command gpu_capture_command(instance.gpu_capture_binary());
    if (gpu_capture_basename == "ngfx") {
      // Crosvm depends on command line arguments being passed as multiple
      // arguments but ngfx only allows a single `--args`. To work around this,
      // create a wrapper script that launches crosvm with all of the arguments
      // and pass this wrapper script to ngfx.
      const std::string crosvm_wrapper_path =
          instance.PerInstanceInternalPath("crosvm_wrapper.sh");
      const std::string crosvm_wrapper_content =
          crosvm_cmd.Cmd().AsBashScript(crosvm_logs_path);

      CF_EXPECT(android::base::WriteStringToFile(crosvm_wrapper_content,
                                                 crosvm_wrapper_path));
      CF_EXPECT(MakeFileExecutable(crosvm_wrapper_path));

      gpu_capture_command.AddParameter("--exe=", crosvm_wrapper_path);
      gpu_capture_command.AddParameter("--launch-detached");
      gpu_capture_command.AddParameter("--verbose");
      gpu_capture_command.AddParameter("--activity=Frame Debugger");
    } else {
      // TODO(natsu): renderdoc
      return CF_ERR(
          "Unhandled GPU capture binary: " << instance.gpu_capture_binary());
    }

    gpu_capture_command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut,
                                      gpu_capture_logs);
    gpu_capture_command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr,
                                      gpu_capture_logs);

    commands.emplace_back(std::move(gpu_capture_log_tee_cmd));
    commands.emplace_back(std::move(gpu_capture_command));
  } else {
    crosvm_cmd.Cmd().RedirectStdIO(Subprocess::StdIOChannel::kStdOut,
                                   crosvm_logs);
    crosvm_cmd.Cmd().RedirectStdIO(Subprocess::StdIOChannel::kStdErr,
                                   crosvm_logs);
    commands.emplace_back(std::move(crosvm_cmd.Cmd()), true);
  }

  return commands;
}

Result<bool> CrosvmManager::WaitForRestoreComplete(SharedFD stop_fd) const {
  auto instance = CF_EXPECT(CuttlefishConfig::Get())->ForDefaultInstance();

  // Wait for the control socket to exist. It is created early in crosvm's
  // startup sequence, but the process may not even have been exec'd by CF at
  // this point.
  while (!FileExists(instance.CrosvmSocketPath())) {
    std::vector<PollSharedFd> poll = {{.fd = stop_fd, .events = POLLIN}};
    const int result = SharedFD::Poll(poll, 50 /* ms */);
    // Check for errors.
    CF_EXPECT(result >= 0, "failed to wait on stop_fd: " << strerror(errno));
    // Check if pipe became readable or closed.
    if (result > 0) {
      return false;
    }
  }

  // Ask crosvm to resume the VM. crosvm promises to not complete this command
  // until the vCPUs are started (even if it was never suspended to begin
  // with).
  auto infop = CF_EXPECT(Execute(
      std::vector<std::string>{
          instance.crosvm_binary(),
          "resume",
          instance.CrosvmSocketPath(),
          "--full",
      },
      SubprocessOptions(), WEXITED));
  CF_EXPECT_EQ(infop.si_code, CLD_EXITED);
  CF_EXPECTF(infop.si_status == 0, "crosvm resume returns non zero code {}",
             infop.si_status);
  return true;
}

}  // namespace vm_manager
}  // namespace cuttlefish
