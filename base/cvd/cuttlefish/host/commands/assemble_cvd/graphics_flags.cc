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

#include "cuttlefish/host/commands/assemble_cvd/graphics_flags.h"

#include <ostream>
#include <string_view>

#include <android-base/file.h>
#include <android-base/strings.h>
#include "absl/strings/str_split.h"
#include <fmt/format.h>
#include <google/protobuf/text_format.h>
#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/host/graphics_detector/graphics_detector.pb.h"
#include "cuttlefish/host/libs/config/config_constants.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/gpu_mode.h"
#include "cuttlefish/host/libs/config/guest_hwui_renderer.h"
#include "cuttlefish/host/libs/config/guest_renderer_preload.h"
#include "cuttlefish/host/libs/config/vmm_mode.h"

#ifdef __APPLE__
#define CF_UNUSED_ON_MACOS [[maybe_unused]]
#else
#define CF_UNUSED_ON_MACOS
#endif

namespace cuttlefish {
namespace {

struct CommonConfig {
  const VmmMode vmm_mode;
  const GuestConfig& guest_config;
  const gfxstream::proto::GraphicsAvailability& graphics_availability;
};

bool HostIsNotArm(const CommonConfig&) { return HostArch() != Arch::Arm64; }

bool GuestSupportsGfxstream(const CommonConfig& common) {
  return common.guest_config.gfxstream_supported;
}

bool NotUsingHostQemu(const CommonConfig& common) {
  return !VmManagerIsQemu(common.vmm_mode) || UseQemuPrebuilt();
}

bool HostHasGles(const CommonConfig& common) {
  const auto& availability = common.graphics_availability;
  const bool has_gles2 =
      availability.has_egl() && availability.egl().has_gles2_availability();
  const bool has_gles3 =
      availability.has_egl() && availability.egl().has_gles3_availability();
  return has_gles2 || has_gles3;
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

bool HostGlesIsNotSoftwareBased(const CommonConfig& common) {
  const auto& availability = common.graphics_availability;
  if (availability.has_egl()) {
    if (availability.egl().has_gles2_availability() &&
        IsLikelySoftwareRenderer(
            availability.egl().gles2_availability().renderer())) {
      return false;
    }
    if (availability.egl().has_gles3_availability() &&
        IsLikelySoftwareRenderer(
            availability.egl().gles3_availability().renderer())) {
      return false;
    }
  }
  return true;
}

bool HostHasVulkan(const CommonConfig& common) {
  const auto& availability = common.graphics_availability;
  return availability.has_vulkan();
}

bool HostVulkanIsNotSoftwareBased(const CommonConfig& common) {
  const auto& availability = common.graphics_availability;
  if (availability.has_vulkan() &&
      !availability.vulkan().physical_devices().empty() &&
      (availability.vulkan().physical_devices(0).type() !=
       ::gfxstream::proto::VulkanPhysicalDevice::TYPE_DISCRETE_GPU)) {
    return false;
  }
  return true;
}

using MeetsRequirementFunc = std::function<bool(const CommonConfig& common)>;

struct RequirementWithReason {
  MeetsRequirementFunc func;
  std::string success_explanation;
  std::string failure_explanation;
};

const std::unordered_map<GpuMode, std::vector<RequirementWithReason>>
    kGpuModeRequirements = {
        {
            GpuMode::Custom,
            /*no requirements*/ {},
        },
        {
            GpuMode::DrmVirgl,
            {
                RequirementWithReason{
                    .func = HostHasGles,
                    .success_explanation = "The host has GLES support.",
                    .failure_explanation =
                        "The host does not have GLES support. Please ensure "
                        "the GLES userspace "
                        "drivers are installed and available.",
                },
            },
        },
        {
            GpuMode::Gfxstream,
            {
                RequirementWithReason{
                    .func = HostIsNotArm,
                    .success_explanation = "The host is not ARM64.",
                    .failure_explanation =
                        "The host is ARM64. Not enabling Gfxstream on ARM64 "
                        "until vhost-user-gpu "
                        "has been more thoroughly tested. Please explicitly "
                        "use "
                        "--gpu_mode=gfxstream or "
                        "--gpu_mode=gfxstream_guest_angle to enable "
                        "for now.",
                },
                RequirementWithReason{
                    .func = GuestSupportsGfxstream,
                    .success_explanation = "The guest supports Gfxstream.",
                    .failure_explanation =
                        "The guest does not support Gfxstream. This is "
                        "configured in the "
                        "`android-info.txt` file associated with the guest "
                        "target build.",
                },
                RequirementWithReason{
                    .func = NotUsingHostQemu,
                    .success_explanation =
                        "The instance is not using the system QEMU.",
                    .failure_explanation =
                        "The instance is using the system QEMU which may not "
                        "have Gfxstream "
                        "support. Please explicitly use --gpu_mode=gfxstream "
                        "or "
                        "--gpu_mode=gfxstream_guest_angle to enable for now.",
                },
                RequirementWithReason{
                    .func = HostHasGles,
                    .success_explanation = "The host has GLES support.",
                    .failure_explanation =
                        "The host does not have GLES support. Please ensure "
                        "the GLES userspace "
                        "drivers are installed and available.",
                },
                RequirementWithReason{
                    .func = HostGlesIsNotSoftwareBased,
                    .success_explanation = "The host GLES driver is not a "
                                           "software implementation.",
                    .failure_explanation =
                        "The host does not have a non-software implementation "
                        "GLES driver. "
                        "Consider enabling "
                        "--gpu_mode=gfxstream_guest_angle_host_swiftshader "
                        "for host software rendering which has a vetted "
                        "software renderer.",
                },
                RequirementWithReason{
                    .func = HostHasVulkan,
                    .success_explanation = "The host has Vulkan support.",
                    .failure_explanation =
                        "The host does not have Vulkan support. Please ensure "
                        "the Vulkan userspace "
                        "drivers and the Vulkan loader are installed and "
                        "available.",
                },
                RequirementWithReason{
                    .func = HostVulkanIsNotSoftwareBased,
                    .success_explanation = "The host Vulkan driver is not a "
                                           "software implementation.",
                    .failure_explanation =
                        "The host does not have a non-software implementation "
                        "Vulkan driver. "
                        "Consider enabling "
                        "--gpu_mode=gfxstream_guest_angle_host_swiftshader "
                        "for host software rendering which has a vetted "
                        "software renderer.",
                },
            },
        },
        {
            GpuMode::GfxstreamGuestAngle,
            {
                RequirementWithReason{
                    .func = HostIsNotArm,
                    .success_explanation = "The host is not ARM64.",
                    .failure_explanation =
                        "The host is ARM64. Not enabling Gfxstream on ARM64 "
                        "until vhost-user-gpu "
                        "has been more thoroughly tested. Please explicitly "
                        "use "
                        "--gpu_mode=gfxstream or "
                        "--gpu_mode=gfxstream_guest_angle to enable "
                        "for now.",
                },
                RequirementWithReason{
                    .func = GuestSupportsGfxstream,
                    .success_explanation = "The guest supports Gfxstream.",
                    .failure_explanation =
                        "The guest does not support Gfxstream. This is "
                        "configured in the "
                        "`android-info.txt` file associated with the guest "
                        "target build.",
                },
                RequirementWithReason{
                    .func = NotUsingHostQemu,
                    .success_explanation =
                        "The instance is not using the system QEMU.",
                    .failure_explanation =
                        "The instance is using the system QEMU which may not "
                        "have Gfxstream "
                        "support. Please explicitly use --gpu_mode=gfxstream "
                        "or "
                        "--gpu_mode=gfxstream_guest_angle to enable for now.",
                },
                RequirementWithReason{
                    .func = HostHasVulkan,
                    .success_explanation = "The host has Vulkan support.",
                    .failure_explanation =
                        "The host does not have Vulkan support. Please ensure "
                        "the Vulkan userspace "
                        "drivers and the Vulkan loader are installed and "
                        "available.",
                },
                RequirementWithReason{
                    .func = HostVulkanIsNotSoftwareBased,
                    .success_explanation = "The host Vulkan driver is not a "
                                           "software implementation.",
                    .failure_explanation =
                        "The host does not have a non-software implementation "
                        "Vulkan driver. "
                        "Consider enabling "
                        "--gpu_mode=gfxstream_guest_angle_host_swiftshader "
                        "for host software rendering which has a vetted "
                        "software renderer.",
                },
            },
        },
        {
            GpuMode::GfxstreamGuestAngleHostSwiftshader,
            {
                RequirementWithReason{
                    .func = HostIsNotArm,
                    .success_explanation = "The host is not ARM64.",
                    .failure_explanation =
                        "The host is ARM64. Not enabling Gfxstream on ARM64 "
                        "until vhost-user-gpu "
                        "has been more thoroughly tested. Please explicitly "
                        "use "
                        "--gpu_mode=gfxstream or "
                        "--gpu_mode=gfxstream_guest_angle to enable "
                        "for now.",
                },
                RequirementWithReason{
                    .func = GuestSupportsGfxstream,
                    .success_explanation = "The guest supports Gfxstream.",
                    .failure_explanation =
                        "The guest does not support Gfxstream. This is "
                        "configured in the "
                        "`android-info.txt` file associated with the guest "
                        "target build.",
                },
                RequirementWithReason{
                    .func = NotUsingHostQemu,
                    .success_explanation =
                        "The instance is not using the system QEMU.",
                    .failure_explanation =
                        "The instance is using the system QEMU which may not "
                        "have Gfxstream "
                        "support. Please explicitly use --gpu_mode=gfxstream "
                        "or "
                        "--gpu_mode=gfxstream_guest_angle to enable for now.",
                },
            },
        },
        {
            GpuMode::GfxstreamGuestAngleHostLavapipe,
            {
                RequirementWithReason{
                    .func = HostIsNotArm,
                    .success_explanation = "The host is not ARM64.",
                    .failure_explanation =
                        "The host is ARM64. Not enabling Gfxstream on ARM64 "
                        "until vhost-user-gpu "
                        "has been more thoroughly tested. Please explicitly "
                        "use "
                        "--gpu_mode=gfxstream or "
                        "--gpu_mode=gfxstream_guest_angle to enable "
                        "for now.",
                },
                RequirementWithReason{
                    .func = GuestSupportsGfxstream,
                    .success_explanation = "The guest supports Gfxstream.",
                    .failure_explanation =
                        "The guest does not support Gfxstream. This is "
                        "configured in the "
                        "`android-info.txt` file associated with the guest "
                        "target build.",
                },
                RequirementWithReason{
                    .func = NotUsingHostQemu,
                    .success_explanation =
                        "The instance is not using the system QEMU.",
                    .failure_explanation =
                        "The instance is using the system QEMU which may not "
                        "have Gfxstream "
                        "support. Please explicitly use --gpu_mode=gfxstream "
                        "or "
                        "--gpu_mode=gfxstream_guest_angle to enable for now.",
                },
            },
        },
        {
            GpuMode::GuestSwiftshader,
            /*no requirements*/ {},
        },
        {
            GpuMode::None,
            /*no requirements*/ {},
        },
};

enum class RenderingMode {
  kNone,
  kCustom,
  kGuestSwiftShader,
  kGfxstream,
  kGfxstreamGuestAngle,
  kGfxstreamGuestAngleHostSwiftshader,
  kGfxstreamGuestAngleHostLavapipe,
  kVirglRenderer,
};

CF_UNUSED_ON_MACOS
Result<RenderingMode> GetRenderingMode(const GpuMode gpu_mode) {
  switch (gpu_mode) {
    case GpuMode::Auto:
      return CF_ERR("Unsupported rendering mode: " << GpuModeString(gpu_mode));
    case GpuMode::Custom:
      return RenderingMode::kCustom;
    case GpuMode::DrmVirgl:
      return RenderingMode::kVirglRenderer;
    case GpuMode::Gfxstream:
      return RenderingMode::kGfxstream;
    case GpuMode::GfxstreamGuestAngle:
      return RenderingMode::kGfxstreamGuestAngle;
    case GpuMode::GfxstreamGuestAngleHostLavapipe:
      return RenderingMode::kGfxstreamGuestAngleHostLavapipe;
    case GpuMode::GfxstreamGuestAngleHostSwiftshader:
      return RenderingMode::kGfxstreamGuestAngleHostSwiftshader;
    case GpuMode::GuestSwiftshader:
      return RenderingMode::kGuestSwiftShader;
    case GpuMode::None:
      return RenderingMode::kNone;
  }
}

struct AngleFeatures {
  // Prefer linear filtering for YUV AHBs to pass
  // android.media.decoder.cts.DecodeAccuracyTest on older branches.
  // Generally not needed after b/315387961.
  bool prefer_linear_filtering_for_yuv = false;

  // Map unspecified color spaces to PASS_THROUGH to pass
  // android.media.codec.cts.DecodeEditEncodeTest and
  // android.media.codec.cts.EncodeDecodeTest.
  bool map_unspecified_color_space_to_pass_through = true;

  // b/264575911: Nvidia seems to have issues with YUV samplers with
  // 'lowp' and 'mediump' precision qualifiers.
  bool ignore_precision_qualifiers = false;

  // ANGLE has a feature to expose 3.2 early even if the device does
  // not fully support all of the 3.2 features. This should be
  // disabled for Cuttlefish as SwiftShader does not have geometry
  // shader nor tesselation shader support.
  bool disable_expose_opengles_3_2_for_testing = false;
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

  if (mode == RenderingMode::kGuestSwiftShader ||
      mode == RenderingMode::kGfxstreamGuestAngleHostSwiftshader) {
    features.disable_expose_opengles_3_2_for_testing = true;
  }

  return features;
}

CF_UNUSED_ON_MACOS
struct AngleFeatureOverrides {
  std::string angle_feature_overrides_enabled;
  std::string angle_feature_overrides_disabled;
};

CF_UNUSED_ON_MACOS
Result<AngleFeatureOverrides> GetNeededAngleFeatures(
    const RenderingMode mode,
    const ::gfxstream::proto::GraphicsAvailability& availability) {
  const AngleFeatures features =
      CF_EXPECT(GetNeededAngleFeaturesBasedOnQuirks(mode, availability));
  VLOG(0) << features;

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
  if (features.disable_expose_opengles_3_2_for_testing) {
    disable_feature_strings.push_back("exposeES32ForTesting");
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

CF_UNUSED_ON_MACOS
Result<VhostUserGpuHostRendererFeatures>
GetNeededVhostUserGpuHostRendererFeatures(
    RenderingMode mode,
    const ::gfxstream::proto::GraphicsAvailability& availability) {
  VhostUserGpuHostRendererFeatures features = {};

  // No features needed for guest rendering.
  if (mode == RenderingMode::kGuestSwiftShader) {
    return features;
  }

  // For any passthrough graphics mode, external blob is needed for sharing
  // buffers between the vhost-user-gpu VMM process and the main VMM process.
  features.external_blob = true;

  // Prebuilt SwiftShader includes VK_EXT_external_memory_host.
  if (mode == RenderingMode::kGfxstreamGuestAngleHostSwiftshader) {
    features.system_blob = true;
  } else {
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
  }

  return features;
}

#ifndef __APPLE__
std::vector<GpuMode> GetGpuModeCandidates(const GuestConfig& guest_config) {
  std::vector<GpuMode> gpu_mode_candidates;

  if (!guest_config.gpu_mode_candidates.empty()) {
    LOG(INFO) << "GPU mode candidates provided by guest.";
    gpu_mode_candidates = guest_config.gpu_mode_candidates;
  } else {
    LOG(INFO) << "GPU mode candidates not provided by guest config. "
                 "Assuming the historically accepted modes.";

    if (guest_config.prefer_drm_virgl_when_supported) {
      gpu_mode_candidates.push_back(GpuMode::DrmVirgl);
    }
    gpu_mode_candidates.push_back(GpuMode::Gfxstream);
    gpu_mode_candidates.push_back(GpuMode::GuestSwiftshader);
    gpu_mode_candidates.push_back(GpuMode::GfxstreamGuestAngle);
    gpu_mode_candidates.push_back(GpuMode::GfxstreamGuestAngleHostLavapipe);
    gpu_mode_candidates.push_back(GpuMode::GfxstreamGuestAngleHostSwiftshader);
    gpu_mode_candidates.push_back(GpuMode::None);
  }

  return gpu_mode_candidates;
}

bool GpuModeRequirementsMet(const CommonConfig& common,
                            const GpuMode gpu_mode) {
  const auto requirements_it = kGpuModeRequirements.find(gpu_mode);
  CHECK(requirements_it != kGpuModeRequirements.end())
      << "Failed to find requirements for mode " << GpuModeString(gpu_mode);
  const auto& requirements = requirements_it->second;

  LOG(INFO) << "Checking requirements for --gpu_mode="
            << GpuModeString(gpu_mode);
  bool all_requirements_met = true;
  for (size_t i = 0; i < requirements.size(); i++) {
    const RequirementWithReason& requirement = requirements[i];
    const bool meets_requirement = requirement.func(common);
    LOG(INFO) << "  " << (i + 1) << ". "
              << (meets_requirement ? "PASSED: " : "FAILED: ")
              << (meets_requirement ? requirement.success_explanation
                                    : requirement.failure_explanation);
    all_requirements_met &= meets_requirement;
  }
  return all_requirements_met;
}

void FilterGpuModeCandidates(const CommonConfig& common,
                             std::vector<GpuMode>& candidates) {
  for (auto candidate_it = candidates.begin();
       candidate_it != candidates.end();) {
    const GpuMode candidate = *candidate_it;
    if (GpuModeRequirementsMet(common, candidate)) {
      LOG(INFO) << "  All requirements met.";
      ++candidate_it;
    } else {
      LOG(INFO)
          << "  All requirements not met. Removing mode from candidate modes.";
      candidate_it = candidates.erase(candidate_it);
    }
    LOG(INFO) << "";
  }
}

GpuMode SelectGpuMode(
    GpuMode gpu_mode, VmmMode vmm, const GuestConfig& guest_config,
    const gfxstream::proto::GraphicsAvailability& graphics_availability) {
  const CommonConfig common = {
      .vmm_mode = vmm,
      .guest_config = guest_config,
      .graphics_availability = graphics_availability,
  };
  if (gpu_mode == GpuMode::Auto) {
    std::vector<GpuMode> gpu_mode_candidates =
        GetGpuModeCandidates(guest_config);
    LOG(INFO) << "Initial GPU mode candidates:";
    for (size_t i = 0; i < gpu_mode_candidates.size(); i++) {
      LOG(INFO) << "  " << (i + 1) << ": "
                << GpuModeString(gpu_mode_candidates[i]);
    }
    LOG(INFO) << "";

    FilterGpuModeCandidates(common, gpu_mode_candidates);
    if (gpu_mode_candidates.empty()) {
      LOG(ERROR) << "Unexpected empty list of candidates...";
      gpu_mode = GpuMode::GuestSwiftshader;
    } else {
      gpu_mode = gpu_mode_candidates[0];
      LOG(INFO) << "\nGPU auto mode: selecting --gpu_mode="
                << GpuModeString(gpu_mode);
    }
  } else {
    // User explicitly supplied a mode. Double check the requirements but only
    // log warnings and respect their choice:
    if (!GpuModeRequirementsMet(common, gpu_mode)) {
      LOG(ERROR)
          << "--gpu_mode=" << GpuModeString(gpu_mode)
          << " was requested but the prerequisites were not detected "
             "so the device may not function correctly. Please consider "
             "switching to --gpu_mode=auto or --gpu_mode=guest_swiftshader.";
    }
  }

  return gpu_mode;
}

Result<bool> SelectGpuVhostUserMode(const GpuMode gpu_mode,
                                    const std::string& gpu_vhost_user_mode_arg,
                                    VmmMode vmm) {
  CF_EXPECT(gpu_vhost_user_mode_arg == kGpuVhostUserModeAuto ||
            gpu_vhost_user_mode_arg == kGpuVhostUserModeOn ||
            gpu_vhost_user_mode_arg == kGpuVhostUserModeOff);
  if (gpu_vhost_user_mode_arg == kGpuVhostUserModeAuto) {
    if (gpu_mode == GpuMode::GuestSwiftshader) {
      LOG(INFO) << "GPU vhost user auto mode: not needed for --gpu_mode="
                << GpuModeString(gpu_mode) << ". Not enabling vhost user gpu.";
      return false;
    }

    if (!VmManagerIsCrosvm(vmm)) {
      LOG(INFO) << "GPU vhost user auto mode: not yet supported with " << vmm
                << ". Not enabling vhost user gpu.";
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

Result<GuestRendererPreload> SelectGuestRendererPreload(
    const GpuMode gpu_mode, const GuestHwuiRenderer guest_hwui_renderer,
    const std::string& guest_renderer_preload_arg) {
  GuestRendererPreload guest_renderer_preload =
      GuestRendererPreload::kGuestDefault;

  if (!guest_renderer_preload_arg.empty()) {
    guest_renderer_preload =
        CF_EXPECT(ParseGuestRendererPreload(guest_renderer_preload_arg));
  }

  if (guest_renderer_preload == GuestRendererPreload::kAuto) {
    if (guest_hwui_renderer == GuestHwuiRenderer::kSkiaVk &&
        (gpu_mode == GpuMode::GfxstreamGuestAngle ||
         gpu_mode == GpuMode::GfxstreamGuestAngleHostSwiftshader)) {
      LOG(INFO) << "Disabling guest renderer preload for Gfxstream based mode "
                   "when running with SkiaVk.";
      guest_renderer_preload = GuestRendererPreload::kDisabled;
    }
  }

  return guest_renderer_preload;
}

#endif

bool IsAmdGpu(const gfxstream::proto::GraphicsAvailability& availability) {
  return (availability.has_egl() &&
          ((availability.egl().has_gles2_availability() &&
            availability.egl().gles2_availability().has_vendor() &&
            availability.egl().gles2_availability().vendor().find("AMD") !=
                std::string::npos) ||
           (availability.egl().has_gles3_availability() &&
            availability.egl().gles3_availability().has_vendor() &&
            availability.egl().gles3_availability().vendor().find("AMD") !=
                std::string::npos))) ||
         (availability.has_vulkan() &&
          !availability.vulkan().physical_devices().empty() &&
          availability.vulkan().physical_devices(0).has_name() &&
          availability.vulkan().physical_devices(0).name().find("AMD") !=
              std::string::npos);
}

const std::string kGfxstreamTransportAsg = "virtio-gpu-asg";
const std::string kGfxstreamTransportPipe = "virtio-gpu-pipe";

CF_UNUSED_ON_MACOS
Result<std::unordered_map<std::string, bool>> ParseGfxstreamRendererFlag(
    const std::string& gpu_renderer_features_arg) {
  std::unordered_map<std::string, bool> features;

  for (const std::string_view feature :
       absl::StrSplit(gpu_renderer_features_arg, ';')) {
    if (feature.empty()) {
      continue;
    }

    const std::vector<std::string_view> feature_parts =
        absl::StrSplit(feature, ':');
    CF_EXPECT(feature_parts.size() == 2,
              "Failed to parse renderer features from --gpu_renderer_features="
                  << gpu_renderer_features_arg);

    const std::string_view feature_name = feature_parts[0];
    const std::string_view feature_enabled = feature_parts[1];
    CF_EXPECT(feature_enabled == "enabled" || feature_enabled == "disabled",
              "Failed to parse renderer features from --gpu_renderer_features="
                  << gpu_renderer_features_arg);

    features.emplace(feature_name, feature_enabled == "enabled");
  }

  return features;
}

CF_UNUSED_ON_MACOS
std::string GetGfxstreamRendererFeaturesString(
    const std::unordered_map<std::string, bool>& features) {
  std::vector<std::string> parts;
  for (const auto& [feature_name, feature_enabled] : features) {
    parts.push_back(feature_name + ":" +
                    (feature_enabled ? "enabled" : "disabled"));
  }
  return android::base::Join(parts, ",");
}

CF_UNUSED_ON_MACOS
Result<void> SetGfxstreamFlags(
    const GpuMode gpu_mode, const std::string& gpu_renderer_features_arg,
    const GuestConfig& guest_config,
    const gfxstream::proto::GraphicsAvailability& availability,
    CuttlefishConfig::MutableInstanceSpecific& instance) {
  std::string gfxstream_transport = kGfxstreamTransportAsg;

  // Some older R branches are missing some Gfxstream backports
  // which introduced a backward incompatible change (b/267483000).
  if (guest_config.android_version_number == "11.0.0") {
    gfxstream_transport = kGfxstreamTransportPipe;
  }

  if (IsAmdGpu(availability)) {
    // KVM does not support mapping host graphics buffers into the guest because
    // the AMD GPU driver uses TTM memory. More info in
    // https://lore.kernel.org/all/20230911021637.1941096-1-stevensd@google.com
    //
    // TODO(b/254721007): replace with a kernel version check after KVM patches
    // land.
    CF_EXPECT(gpu_mode != GpuMode::GfxstreamGuestAngle,
              "--gpu_mode=gfxstream_guest_angle is broken on AMD GPUs.");
  }

  std::unordered_map<std::string, bool> features;

  // Apply features from host/mode requirements.
  if (gpu_mode == GpuMode::GfxstreamGuestAngleHostSwiftshader) {
    features["VulkanUseDedicatedAhbMemoryType"] = true;
  }

  // Apply features from guest/mode requirements.
  if (guest_config.gfxstream_gl_program_binary_link_status_supported) {
    features["GlProgramBinaryLinkStatus"] = true;
  }

  // Apply feature overrides from --gpu_renderer_features.
  const auto feature_overrides =
      CF_EXPECT(ParseGfxstreamRendererFlag(gpu_renderer_features_arg));
  for (const auto& [feature_name, feature_enabled] : feature_overrides) {
    VLOG(0) << "GPU renderer feature " << feature_name << " overridden to "
            << (feature_enabled ? "enabled" : "disabled")
            << " via command line argument.";
    features[feature_name] = feature_enabled;
  }

  // Convert features back to a string for passing to the VMM.
  const std::string features_string =
      GetGfxstreamRendererFeaturesString(features);
  if (!features_string.empty()) {
    instance.set_gpu_renderer_features(features_string);
  }

  instance.set_gpu_gfxstream_transport(gfxstream_transport);
  return {};
}

static std::unordered_set<std::string> kSupportedGpuContexts{
    "gfxstream-vulkan", "gfxstream-composer", "cross-domain", "magma"};

}  // namespace

gfxstream::proto::GraphicsAvailability
GetGraphicsAvailabilityWithSubprocessCheck() {
#ifdef __APPLE__
  return {};
#else
  TemporaryFile graphics_availability_file;

  Command graphics_detector_cmd(HostBinaryPath("graphics_detector"));
  graphics_detector_cmd.AddParameter(graphics_availability_file.path);

  Result<std::string> graphics_detector_stdout =
      RunAndCaptureStdout(std::move(graphics_detector_cmd));
  if (!graphics_detector_stdout.ok()) {
    LOG(ERROR)
        << "Failed to run graphics detector, assuming no availability: \n"
        << graphics_detector_stdout.error();
    return {};
  }
  VLOG(0) << *graphics_detector_stdout;

  auto graphics_availability_content_result =
      ReadFileContents(graphics_availability_file.path);
  if (!graphics_availability_content_result.ok()) {
    LOG(ERROR) << "Failed to read graphics availability from file "
               << graphics_availability_file.path << ":"
               << graphics_availability_content_result.error()
               << ". Assuming no availability.";
    return {};
  }
  const std::string& graphics_availability_content =
      graphics_availability_content_result.value();

  gfxstream::proto::GraphicsAvailability availability;
  google::protobuf::TextFormat::Parser parser;
  if (!parser.ParseFromString(graphics_availability_content, &availability)) {
    LOG(ERROR) << "Failed to parse graphics detector output: "
               << graphics_availability_content
               << ". Assuming no availability.";
    return {};
  }

  VLOG(0) << "Host Graphics Availability:" << availability.DebugString();
  return availability;
#endif
}

Result<GpuMode> ConfigureGpuSettings(
    const gfxstream::proto::GraphicsAvailability& graphics_availability,
    GpuMode gpu_mode_arg, const std::string& gpu_vhost_user_mode_arg,
    const std::string& gpu_renderer_features_arg,
    std::string& gpu_context_types_arg,
    const std::string& guest_hwui_renderer_arg,
    const std::string& guest_renderer_preload_arg, VmmMode vmm,
    const GuestConfig& guest_config,
    CuttlefishConfig::MutableInstanceSpecific& instance) {
#ifdef __APPLE__
  (void)graphics_availability;
  (void)gpu_vhost_user_mode_arg;
  (void)vmm;
  (void)guest_config;
  CF_EXPECT(gpu_mode_arg == GpuMode::Auto ||
            gpu_mode_arg == GpuMode::GuestSwiftshader ||
            gpu_mode_arg == GpuMode::DrmVirgl || gpu_mode == GpuMode::None);
  if (gpu_mode_arg == GpuMode::Auto) {
    gpu_mode_arg = GpuMode::GuestSwiftshader;
  }
  instance.set_gpu_mode(gpu_mode_arg);
  instance.set_enable_gpu_vhost_user(false);
#else
  const GpuMode gpu_mode =
      SelectGpuMode(gpu_mode_arg, vmm, guest_config, graphics_availability);
  const bool enable_gpu_vhost_user =
      CF_EXPECT(SelectGpuVhostUserMode(gpu_mode, gpu_vhost_user_mode_arg, vmm));

  if (IsGfxstreamMode(gpu_mode)) {
    CF_EXPECT(SetGfxstreamFlags(gpu_mode, gpu_renderer_features_arg,
                                guest_config, graphics_availability, instance));
  }

  if (gpu_mode == GpuMode::Custom) {
    std::vector<std::string> requested_types =
        absl::StrSplit(gpu_context_types_arg, ':');
    for (const std::string& requested : requested_types) {
      CF_EXPECT(kSupportedGpuContexts.count(requested) == 1,
                "unsupported context type: " + requested);
    }
  }

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

  GuestHwuiRenderer hwui_renderer = GuestHwuiRenderer::kUnknown;
  if (!guest_hwui_renderer_arg.empty()) {
    hwui_renderer = CF_EXPECT(
        ParseGuestHwuiRenderer(guest_hwui_renderer_arg),
        "Failed to parse HWUI renderer flag: " << guest_hwui_renderer_arg);
  }
  instance.set_guest_hwui_renderer(hwui_renderer);

  const auto guest_renderer_preload = CF_EXPECT(SelectGuestRendererPreload(
      gpu_mode, hwui_renderer, guest_renderer_preload_arg));
  instance.set_guest_renderer_preload(guest_renderer_preload);

  instance.set_gpu_mode(gpu_mode);
  instance.set_enable_gpu_vhost_user(enable_gpu_vhost_user);

#endif

  return gpu_mode;
}

}  // namespace cuttlefish
