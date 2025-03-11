/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/assemble_cvd/graphics_flags.h"

#include <ostream>

#include <GraphicsDetector.pb.h>
#include <android-base/strings.h>
#include <fmt/format.h>
#include <google/protobuf/text_format.h>

#include "common/libs/utils/contains.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"

namespace cuttlefish {
namespace {

enum class RenderingMode {
  kNone,
  kGuestSwiftShader,
  kGfxstream,
  kGfxstreamGuestAngle,
  kGfxstreamGuestAngleHostSwiftshader,
  kVirglRenderer,
};

Result<RenderingMode> GetRenderingMode(const std::string& mode) {
  if (mode == std::string(kGpuModeDrmVirgl)) {
    return RenderingMode::kVirglRenderer;
  }
  if (mode == std::string(kGpuModeGfxstream)) {
    return RenderingMode::kGfxstream;
  }
  if (mode == std::string(kGpuModeGfxstreamGuestAngle)) {
    return RenderingMode::kGfxstreamGuestAngle;
  }
  if (mode == std::string(kGpuModeGfxstreamGuestAngleHostSwiftShader)) {
    return RenderingMode::kGfxstreamGuestAngleHostSwiftshader;
  }
  if (mode == std::string(kGpuModeGuestSwiftshader)) {
    return RenderingMode::kGuestSwiftShader;
  }
  if (mode == std::string(kGpuModeNone)) {
    return RenderingMode::kNone;
  }
  return CF_ERR("Unsupported rendering mode: " << mode);
}

struct AngleFeatures {
  // Prefer linear filtering for YUV AHBs to pass
  // android.media.decoder.cts.DecodeAccuracyTest.
  bool prefer_linear_filtering_for_yuv = true;

  // Map unspecified color spaces to PASS_THROUGH to pass
  // android.media.codec.cts.DecodeEditEncodeTest and
  // android.media.codec.cts.EncodeDecodeTest.
  bool map_unspecified_color_space_to_pass_through = true;

  // b/264575911: Nvidia seems to have issues with YUV samplers with
  // 'lowp' and 'mediump' precision qualifiers.
  bool ignore_precision_qualifiers = false;
};

std::ostream& operator<<(std::ostream& stream, const AngleFeatures& features) {
  fmt::print(stream, "ANGLE features: \n");
  fmt::print(stream, " - prefer_linear_filtering_for_yuv: {}\n",
             features.prefer_linear_filtering_for_yuv);
  fmt::print(stream, " - map_unspecified_color_space_to_pass_through: {}\n",
             features.map_unspecified_color_space_to_pass_through);
  fmt::print(stream, " - ignore_precision_qualifiers: {}\n",
             features.ignore_precision_qualifiers);
  return stream;
}

Result<AngleFeatures> GetNeededAngleFeaturesBasedOnQuirks(
    const RenderingMode mode,
    const ::gfxstream::proto::GraphicsAvailability& availability) {
  AngleFeatures features = {};
  if (mode == RenderingMode::kGfxstreamGuestAngle) {
    if (availability.has_vulkan() &&
        !availability.vulkan().physical_devices().empty() &&
        availability.vulkan().physical_devices(0).has_quirks() &&
        availability.vulkan()
            .physical_devices(0)
            .quirks()
            .has_issue_with_precision_qualifiers_on_yuv_samplers()) {
      features.ignore_precision_qualifiers = true;
    }
  }
  return features;
}

std::string ToLower(const std::string& v) {
  std::string result = v;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

bool IsLikelySoftwareRenderer(const std::string& renderer) {
  const std::string lower_renderer = ToLower(renderer);
  return lower_renderer.find("llvmpipe") != std::string::npos;
}

bool ShouldEnableAcceleratedRendering(
    const ::gfxstream::proto::GraphicsAvailability& availability) {
  const bool sufficient_gles2 =
      availability.has_egl() && availability.egl().has_gles2_availability() &&
      !IsLikelySoftwareRenderer(
          availability.egl().gles2_availability().renderer());
  const bool sufficient_gles3 =
      availability.has_egl() && availability.egl().has_gles3_availability() &&
      !IsLikelySoftwareRenderer(
          availability.egl().gles3_availability().renderer());
  const bool has_discrete_gpu =
      availability.has_vulkan() &&
      !availability.vulkan().physical_devices().empty() &&
      (availability.vulkan().physical_devices(0).type() ==
       ::gfxstream::proto::VulkanPhysicalDevice::TYPE_DISCRETE_GPU);
  return (sufficient_gles2 || sufficient_gles3) && has_discrete_gpu;
}

struct AngleFeatureOverrides {
  std::string angle_feature_overrides_enabled;
  std::string angle_feature_overrides_disabled;
};

Result<AngleFeatureOverrides> GetNeededAngleFeatures(
    const RenderingMode mode,
    const ::gfxstream::proto::GraphicsAvailability& availability) {
  const AngleFeatures features =
      CF_EXPECT(GetNeededAngleFeaturesBasedOnQuirks(mode, availability));
  LOG(DEBUG) << features;

  std::vector<std::string> enable_feature_strings;
  std::vector<std::string> disable_feature_strings;
  if (features.prefer_linear_filtering_for_yuv) {
    enable_feature_strings.push_back("preferLinearFilterForYUV");
  }
  if (features.map_unspecified_color_space_to_pass_through) {
    enable_feature_strings.push_back("mapUnspecifiedColorSpaceToPassThrough");
  }
  if (features.ignore_precision_qualifiers) {
    disable_feature_strings.push_back("enablePrecisionQualifiers");
  }

  return AngleFeatureOverrides{
      .angle_feature_overrides_enabled =
          android::base::Join(enable_feature_strings, ':'),
      .angle_feature_overrides_disabled =
          android::base::Join(disable_feature_strings, ':'),
  };
}

struct VhostUserGpuHostRendererFeatures {
  // If true, host Virtio GPU blob resources will be allocated with
  // external memory and exported file descriptors will be shared
  // with the VMM for mapping resources into the guest address space.
  bool external_blob = false;

  // If true, host Virtio GPU blob resources will be allocated with
  // shmem and exported file descriptors will be shared with the VMM
  // for mapping resources into the guest address space.
  //
  // This is an extension of the above external_blob that allows the
  // VMM to map resources without graphics API support but requires
  // additional features (VK_EXT_external_memory_host) from the GPU
  // driver and is potentially less performant.
  bool system_blob = false;
};

Result<VhostUserGpuHostRendererFeatures>
GetNeededVhostUserGpuHostRendererFeatures(
    RenderingMode mode,
    const ::gfxstream::proto::GraphicsAvailability& availability) {
  VhostUserGpuHostRendererFeatures features = {};

  CF_EXPECT(
      mode == RenderingMode::kGfxstream ||
          mode == RenderingMode::kGfxstreamGuestAngle,
      "vhost-user-gpu is only currently supported with --gpu_mode=gfxstream "
      "and --gpu_mode=gfxstream_guest_angle");

  features.external_blob = true;

  const bool has_external_memory_host =
      availability.has_vulkan() &&
      !availability.vulkan().physical_devices().empty() &&
      Contains(availability.vulkan().physical_devices(0).extensions(),
               "VK_EXT_external_memory_host");

  CF_EXPECT(
      has_external_memory_host || mode != RenderingMode::kGfxstreamGuestAngle,
      "VK_EXT_external_memory_host is required for running with "
      "--gpu_mode=gfxstream_guest_angle and --enable_gpu_vhost_user=true");

  features.system_blob = has_external_memory_host;

  return features;
}

#ifndef __APPLE__
Result<std::string> SelectGpuMode(
    const std::string& gpu_mode_arg, const std::string& vm_manager,
    const GuestConfig& guest_config,
    const gfxstream::proto::GraphicsAvailability& graphics_availability) {
  if (gpu_mode_arg != kGpuModeAuto && gpu_mode_arg != kGpuModeDrmVirgl &&
      gpu_mode_arg != kGpuModeGfxstream &&
      gpu_mode_arg != kGpuModeGfxstreamGuestAngle &&
      gpu_mode_arg != kGpuModeGfxstreamGuestAngleHostSwiftShader &&
      gpu_mode_arg != kGpuModeGuestSwiftshader &&
      gpu_mode_arg != kGpuModeNone) {
    return CF_ERR("Invalid gpu_mode: " << gpu_mode_arg);
  }

  if (gpu_mode_arg == kGpuModeAuto) {
    // TODO (280826461) Android T Cuttlefish is currently not compatible
    // with accelerated graphics.
    if (guest_config.android_version_number == "13.0.0" ||
        guest_config.android_version_number == "13") {
      LOG(INFO) << "GPU auto mode: detected guest of version T"
                << ". Accelerated rendering support is not compatible, "
                   "enabling --gpu_mode=guest_swiftshader.";
      return kGpuModeGuestSwiftshader;
    }

    if (vm_manager == vm_manager::QemuManager::name() &&
        !IsHostCompatible(guest_config.target_arch)) {
      LOG(INFO) << "Enabling --gpu_mode=drm_virgl.";
      return kGpuModeDrmVirgl;
    }

    if (ShouldEnableAcceleratedRendering(graphics_availability)) {
      LOG(INFO) << "GPU auto mode: detected prerequisites for accelerated "
                << "rendering support.";
      if (vm_manager == vm_manager::QemuManager::name()) {
        LOG(INFO) << "Enabling --gpu_mode=drm_virgl.";
        return kGpuModeDrmVirgl;
      } else {
        LOG(INFO) << "Enabling --gpu_mode=gfxstream.";
        return kGpuModeGfxstream;
      }
    } else {
      LOG(INFO) << "GPU auto mode: did not detect prerequisites for "
                   "accelerated rendering support, enabling "
                   "--gpu_mode=guest_swiftshader.";
      return kGpuModeGuestSwiftshader;
    }
  }

  if (gpu_mode_arg == kGpuModeGfxstream ||
      gpu_mode_arg == kGpuModeGfxstreamGuestAngle ||
      gpu_mode_arg == kGpuModeDrmVirgl) {
    if (!ShouldEnableAcceleratedRendering(graphics_availability)) {
      LOG(ERROR) << "--gpu_mode=" << gpu_mode_arg
                 << " was requested but the prerequisites for accelerated "
                    "rendering were not detected so the device may not "
                    "function correctly. Please consider switching to "
                    "--gpu_mode=auto or --gpu_mode=guest_swiftshader.";
    }
  }

  return gpu_mode_arg;
}

Result<bool> SelectGpuVhostUserMode(const std::string& gpu_mode,
                                    const std::string& gpu_vhost_user_mode_arg,
                                    const std::string& vm_manager) {
  CF_EXPECT(gpu_vhost_user_mode_arg == kGpuVhostUserModeAuto ||
            gpu_vhost_user_mode_arg == kGpuVhostUserModeOn ||
            gpu_vhost_user_mode_arg == kGpuVhostUserModeOff);
  if (gpu_vhost_user_mode_arg == kGpuVhostUserModeAuto) {
    if (gpu_mode == kGpuModeGuestSwiftshader ||
        gpu_mode == kGpuModeGfxstreamGuestAngleHostSwiftShader) {
      LOG(INFO) << "GPU vhost user auto mode: not needed for --gpu_mode="
                << gpu_mode << ". Not enabling vhost user gpu.";
      return false;
    }

    if (vm_manager != vm_manager::CrosvmManager::name()) {
      LOG(INFO) << "GPU vhost user auto mode: not yet supported with "
                << vm_manager << ". Not enabling vhost user gpu.";
      return false;
    }

    // Android built ARM host tools seem to be incompatible with host GPU
    // libraries. Enable vhost user gpu which will run the virtio GPU device
    // in a separate process with a VMM prebuilt. See b/200592498.
    const auto host_arch = HostArch();
    if (host_arch == Arch::Arm64) {
      LOG(INFO) << "GPU vhost user auto mode: detected arm64 host. Enabling "
                   "vhost user gpu.";
      return true;
    }

    LOG(INFO) << "GPU vhost user auto mode: not needed. Not enabling vhost "
                 "user gpu.";
    return false;
  }

  return gpu_vhost_user_mode_arg == kGpuVhostUserModeOn;
}

#endif

Result<std::string> GraphicsDetectorBinaryPath() {
  const auto host_arch = HostArch();
  switch (host_arch) {
    case Arch::Arm64:
      return HostBinaryPath("aarch64-linux-gnu/gfxstream_graphics_detector");
    case Arch::X86:
    case Arch::X86_64:
      return HostBinaryPath("x86_64-linux-gnu/gfxstream_graphics_detector");
    default:
      break;
  }
  return CF_ERR("Graphics detector unavailable for host arch.");
}

Result<const gfxstream::proto::GraphicsAvailability>
GetGraphicsAvailabilityWithSubprocessCheck() {
  Command graphics_detector_cmd(CF_EXPECT(GraphicsDetectorBinaryPath()));
  std::string graphics_detector_stdout;
  auto ret = RunWithManagedStdio(std::move(graphics_detector_cmd), nullptr,
                                 &graphics_detector_stdout, nullptr);
  CF_EXPECT_EQ(ret, 0, "Failed to run graphics detector, bad return value");

  gfxstream::proto::GraphicsAvailability availability;
  google::protobuf::TextFormat::Parser parser;
  if (!parser.ParseFromString(graphics_detector_stdout, &availability)) {
    return CF_ERR("Failed to parse graphics detector stdout: "
                  << graphics_detector_stdout);
  }

  return availability;
}

}  // namespace

Result<std::string> ConfigureGpuSettings(
    const std::string& gpu_mode_arg, const std::string& gpu_vhost_user_mode_arg,
    const std::string& vm_manager, const GuestConfig& guest_config,
    CuttlefishConfig::MutableInstanceSpecific& instance) {
#ifdef __APPLE__
  (void)gpu_vhost_user_mode_arg;
  (void)vm_manager;
  (void)guest_config;
  CF_EXPECT(gpu_mode_arg == kGpuModeAuto ||
            gpu_mode_arg == kGpuModeGuestSwiftshader ||
            gpu_mode_arg == kGpuModeDrmVirgl || gpu_mode_arg == kGpuModeNone);
  std::string gpu_mode = gpu_mode_arg;
  if (gpu_mode == kGpuModeAuto) {
    gpu_mode = kGpuModeGuestSwiftshader;
  }
  instance.set_gpu_mode(gpu_mode);
  instance.set_enable_gpu_vhost_user(false);
#else
  gfxstream::proto::GraphicsAvailability graphics_availability;

  auto graphics_availability_result =
      GetGraphicsAvailabilityWithSubprocessCheck();
  if (!graphics_availability_result.ok()) {
    LOG(ERROR) << "Failed to get graphics availability. Assuming none.";
  } else {
    graphics_availability = graphics_availability_result.value();
    LOG(DEBUG) << "Host Graphics Availability:"
               << graphics_availability.DebugString();
  }

  const std::string gpu_mode = CF_EXPECT(SelectGpuMode(
      gpu_mode_arg, vm_manager, guest_config, graphics_availability));
  const bool enable_gpu_vhost_user = CF_EXPECT(
      SelectGpuVhostUserMode(gpu_mode, gpu_vhost_user_mode_arg, vm_manager));

  const auto angle_features = CF_EXPECT(GetNeededAngleFeatures(
      CF_EXPECT(GetRenderingMode(gpu_mode)), graphics_availability));
  instance.set_gpu_angle_feature_overrides_enabled(
      angle_features.angle_feature_overrides_enabled);
  instance.set_gpu_angle_feature_overrides_disabled(
      angle_features.angle_feature_overrides_disabled);

  if (enable_gpu_vhost_user) {
    const auto gpu_vhost_user_features =
        CF_EXPECT(GetNeededVhostUserGpuHostRendererFeatures(
            CF_EXPECT(GetRenderingMode(gpu_mode)), graphics_availability));
    instance.set_enable_gpu_external_blob(
        gpu_vhost_user_features.external_blob);
    instance.set_enable_gpu_system_blob(gpu_vhost_user_features.system_blob);
  } else {
    instance.set_enable_gpu_external_blob(false);
    instance.set_enable_gpu_system_blob(false);
  }

  instance.set_gpu_mode(gpu_mode);
  instance.set_enable_gpu_vhost_user(enable_gpu_vhost_user);
#endif

  return gpu_mode;
}

}  // namespace cuttlefish
