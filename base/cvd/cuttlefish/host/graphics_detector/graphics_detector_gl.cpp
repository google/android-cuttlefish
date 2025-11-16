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

#include "cuttlefish/host/graphics_detector/graphics_detector_gl.h"

#include "cuttlefish/host/graphics_detector/egl.h"
#include "cuttlefish/host/graphics_detector/gles.h"

namespace gfxstream {
namespace {

using ::gfxstream::proto::EglAvailability;

using GlesContextAvailability =
    ::gfxstream::proto::EglAvailability::GlesContextAvailability;
using PlatformAvailability =
    ::gfxstream::proto::EglAvailability::PlatformAvailability;

constexpr const char kSurfacelessContextExt[] = "EGL_KHR_surfaceless_context";

class Closer {
 public:
  Closer(std::function<void()> on_close) : on_close_(std::move(on_close)) {}
  ~Closer() { on_close_(); }

 private:
  std::function<void()> on_close_;
};

enum class GlesLoadMethod {
  VIA_EGL,
  VIA_GLESV2,
};

gfxstream::expected<GlesContextAvailability, std::string>
GetGlesContextAvailability(Egl& egl, EGLDisplay eglDisplay, EGLConfig eglConfig,
                           EGLint contextVersion, GlesLoadMethod loadMethod) {
  GlesContextAvailability availability;

  const EGLint contextAttributes[] = {
      // clang-format off
        EGL_CONTEXT_CLIENT_VERSION, contextVersion,
        EGL_NONE,
      // clang-format on
  };

  EGLContext context = egl.eglCreateContext(eglDisplay, eglConfig,
                                            EGL_NO_CONTEXT, contextAttributes);
  if (context == EGL_NO_CONTEXT) {
    return gfxstream::unexpected("Failed to create context.");
  }
  Closer contextCloser([&]() { egl.eglDestroyContext(eglDisplay, context); });

  if (egl.eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, context) !=
      EGL_TRUE) {
    return gfxstream::unexpected("Failed to make context current.");
  }

  auto gles = loadMethod == GlesLoadMethod::VIA_EGL
                  ? GFXSTREAM_EXPECT(Gles::LoadFromEgl(&egl))
                  : GFXSTREAM_EXPECT(Gles::Load());

  const GLubyte* gles_vendor = gles.glGetString(GL_VENDOR);
  if (gles_vendor == nullptr) {
    return gfxstream::unexpected("Failed to query vendor.");
  }
  const std::string gles_vendor_string((const char*)gles_vendor);
  availability.set_vendor(gles_vendor_string);

  const GLubyte* gles_version = gles.glGetString(GL_VERSION);
  if (gles_version == nullptr) {
    gfxstream::unexpected("Failed to query vendor.");
  }
  const std::string gles_version_string((const char*)gles_version);
  availability.set_version(gles_version_string);

  const GLubyte* gles_renderer = gles.glGetString(GL_RENDERER);
  if (gles_renderer == nullptr) {
    gfxstream::unexpected("Failed to query renderer.");
  }
  const std::string gles_renderer_string((const char*)gles_renderer);
  availability.set_renderer(gles_renderer_string);

  const GLubyte* gles_extensions = gles.glGetString(GL_EXTENSIONS);
  if (gles_extensions == nullptr) {
    return gfxstream::unexpected("Failed to query extensions.");
  }
  const std::string gles_extensions_string((const char*)gles_extensions);
  availability.set_extensions(gles_extensions_string);

  return availability;
}

gfxstream::expected<Ok, std::string> PopulatePlatformAvailability(
    Egl& egl, EGLDisplay display, PlatformAvailability& availability) {
  EGLint client_version_major = 0;
  EGLint client_version_minor = 0;
  if (egl.eglInitialize(display, &client_version_major,
                        &client_version_minor) != EGL_TRUE) {
    return gfxstream::unexpected("Failed to initialize display.");
  }

  const std::string version_string = egl.eglQueryString(display, EGL_VERSION);
  if (version_string.empty()) {
    return gfxstream::unexpected("Failed to query client version.");
  }
  availability.set_version(version_string);

  const std::string vendor_string = egl.eglQueryString(display, EGL_VENDOR);
  if (vendor_string.empty()) {
    return gfxstream::unexpected("Failed to query vendor.");
  }
  availability.set_vendor(vendor_string);

  const std::string extensions_string =
      egl.eglQueryString(display, EGL_EXTENSIONS);
  if (extensions_string.empty()) {
    return gfxstream::unexpected("Failed to query extensions.");
  }
  availability.set_extensions(extensions_string);

  if (extensions_string.find(kSurfacelessContextExt) == std::string::npos) {
    return gfxstream::unexpected(
        "Failed to find extension EGL_KHR_surfaceless_context.");
  }

  const std::string display_apis_string =
      egl.eglQueryString(display, EGL_CLIENT_APIS);
  if (display_apis_string.empty()) {
    return gfxstream::unexpected("Failed to query display apis.");
  }

  if (egl.eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
    return gfxstream::unexpected("Failed to bind GLES API.");
  }

  const EGLint framebufferConfigAttributes[] = {
      // clang-format off
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,        1,
        EGL_GREEN_SIZE,      1,
        EGL_BLUE_SIZE,       1,
        EGL_ALPHA_SIZE,      0,
        EGL_NONE,
      // clang-format on
  };

  EGLConfig framebufferConfig;
  EGLint numFramebufferConfigs = 0;
  if (egl.eglChooseConfig(display, framebufferConfigAttributes,
                          &framebufferConfig, 1,
                          &numFramebufferConfigs) != EGL_TRUE) {
    return gfxstream::unexpected("Failed to find matching framebuffer config.");
  }

  struct GlesContextCheckOptions {
    std::function<GlesContextAvailability*()> availabilityProvider;
    EGLint contextVersion;
    GlesLoadMethod loadMethod;

    std::string to_string() const {
      std::string ret;
      ret += "options {";

      ret += " version: ";
      ret += std::to_string(contextVersion);

      ret += " load-method: ";
      ret += loadMethod == GlesLoadMethod::VIA_EGL ? "via-egl" : "via-glesv2";

      ret += " }";
      return ret;
    }
  };
  const std::vector<GlesContextCheckOptions> contextChecks = {
      GlesContextCheckOptions{
          .availabilityProvider =
              [&]() { return availability.mutable_gles2_availability(); },
          .contextVersion = 2,
          .loadMethod = GlesLoadMethod::VIA_EGL,
      },
      GlesContextCheckOptions{
          .availabilityProvider =
              [&]() {
                return availability.mutable_gles2_direct_availability();
              },
          .contextVersion = 2,
          .loadMethod = GlesLoadMethod::VIA_GLESV2,
      },
      GlesContextCheckOptions{
          .availabilityProvider =
              [&]() { return availability.mutable_gles3_availability(); },
          .contextVersion = 3,
          .loadMethod = GlesLoadMethod::VIA_EGL,
      },
      GlesContextCheckOptions{
          .availabilityProvider =
              [&]() {
                return availability.mutable_gles3_direct_availability();
              },
          .contextVersion = 3,
          .loadMethod = GlesLoadMethod::VIA_GLESV2,
      },
  };

  for (const GlesContextCheckOptions& contextCheck : contextChecks) {
    auto contextCheckResult = GetGlesContextAvailability(
        egl, display, framebufferConfig, contextCheck.contextVersion,
        contextCheck.loadMethod);
    if (contextCheckResult.ok()) {
      *contextCheck.availabilityProvider() = contextCheckResult.value();
    } else {
      availability.add_errors("Failed to complete GLES context check using " +
                              contextCheck.to_string() + ": " +
                              contextCheckResult.error());
    }
  }

  return Ok{};
}

void CopyPlatformAvailabilityToEglAvailabilityForBackCompat(
    const PlatformAvailability* platform_availability,
    EglAvailability* egl_availability) {
  if (platform_availability->has_vendor()) {
    egl_availability->set_vendor(platform_availability->vendor());
  }
  if (platform_availability->has_version()) {
    egl_availability->set_version(platform_availability->version());
  }
  if (platform_availability->has_extensions()) {
    egl_availability->set_extensions(platform_availability->extensions());
  }
  if (platform_availability->has_gles2_availability()) {
    *egl_availability->mutable_gles2_availability() =
        platform_availability->gles2_availability();
  }
  if (platform_availability->has_gles3_availability()) {
    *egl_availability->mutable_gles3_availability() =
        platform_availability->gles3_availability();
  }
  if (platform_availability->has_gles2_direct_availability()) {
    *egl_availability->mutable_gles2_direct_availability() =
        platform_availability->gles2_direct_availability();
  }
  if (platform_availability->has_gles3_direct_availability()) {
    *egl_availability->mutable_gles3_direct_availability() =
        platform_availability->gles3_direct_availability();
  }
}

gfxstream::expected<Ok, std::string> PopulateDefaultPlatformAvailability(
    Egl& egl, EglAvailability* egl_availability) {
  EGLDisplay display = egl.eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display == EGL_NO_DISPLAY) {
    return gfxstream::unexpected(
        "Failed to get display for EGL_DEFAULT_DISPLAY.");
  }

  PlatformAvailability* platform_availability =
      egl_availability->mutable_default_platform_availability();

  auto result =
      PopulatePlatformAvailability(egl, display, *platform_availability);
  if (!result.ok()) {
    platform_availability->add_errors(
        "Failed to complete platform availability check: " + result.error());
  }

  CopyPlatformAvailabilityToEglAvailabilityForBackCompat(platform_availability,
                                                         egl_availability);

  return Ok{};
}

gfxstream::expected<Ok, std::string> PopulateExplicitPlatformAvailabilities(
    Egl& egl, const std::string& client_extensions,
    EglAvailability* egl_availability) {
  if (client_extensions.find("EGL_EXT_platform_base") == std::string::npos) {
    return Ok{};
  }

  if (egl.eglGetPlatformDisplayEXT == nullptr) {
    return gfxstream::unexpected(
        "Failed to complete platform availability checks: has "
        "EGL_EXT_platform_base but failed to load "
        "eglGetPlatformDisplayEXT().");
  }

  struct EglPlatformCheckOption {
    EGLenum platform;
    std::string platform_string;
    // The extension required to use this platform.
    std::string platform_extension;
    // Functor so that the sub-proto is not populated if the extension is not
    // present.
    std::function<PlatformAvailability*()> platform_availability_getter;
  };
  const std::vector<EglPlatformCheckOption> platforms_to_check = {
      EglPlatformCheckOption{
          .platform = EGL_PLATFORM_GBM_KHR,
          .platform_string = "EGL_PLATFORM_GBM_KHR",
          .platform_extension = "EGL_KHR_platform_gbm",
          .platform_availability_getter =
              [&]() {
                return egl_availability->mutable_gbm_platform_availability();
              },
      },
      EglPlatformCheckOption{
          .platform = EGL_PLATFORM_SURFACELESS_MESA,
          .platform_string = "EGL_PLATFORM_SURFACELESS_MESA",
          .platform_extension = "EGL_MESA_platform_surfaceless",
          .platform_availability_getter =
              [&]() {
                return egl_availability
                    ->mutable_surfaceless_mesa_platform_availability();
              },
      },
      EglPlatformCheckOption{
          .platform = EGL_PLATFORM_WAYLAND_EXT,
          .platform_string = "EGL_PLATFORM_WAYLAND_EXT",
          .platform_extension = "EGL_EXT_platform_wayland",
          .platform_availability_getter =
              [&]() {
                return egl_availability
                    ->mutable_wayland_platform_availability();
              },
      },
      EglPlatformCheckOption{
          .platform = EGL_PLATFORM_X11_EXT,
          .platform_string = "EGL_PLATFORM_X11_EXT",
          .platform_extension = "EGL_EXT_platform_x11",
          .platform_availability_getter =
              [&]() {
                return egl_availability->mutable_x11_platform_availability();
              },
      },
  };
  for (const EglPlatformCheckOption& platform : platforms_to_check) {
    if (client_extensions.find(platform.platform_extension) ==
        std::string::npos) {
      continue;
    }

    PlatformAvailability* platform_availability =
        platform.platform_availability_getter();

    EGLDisplay display = egl.eglGetPlatformDisplayEXT(
        platform.platform, reinterpret_cast<void*>(EGL_DEFAULT_DISPLAY), NULL);
    if (display == EGL_NO_DISPLAY) {
      platform_availability->add_errors(
          "Failed to complete platform availability check: "
          "eglGetPlatformDisplayEXT(" +
          platform.platform_string + ") returned EGL_NO_DISPLAY.");
      continue;
    }

    auto result =
        PopulatePlatformAvailability(egl, display, *platform_availability);
    if (!result.ok()) {
      platform_availability->add_errors(
          "Failed to complete platform availability check: " + result.error());
      continue;
    }
  }

  return Ok{};
}

gfxstream::expected<Ok, std::string>
PopulateExplicitDevicePlatformAvailabilities(
    Egl& egl, const std::string& client_extensions,
    EglAvailability* egl_availability) {
  if (client_extensions.find("EGL_EXT_platform_base") == std::string::npos ||
      client_extensions.find("EGL_EXT_platform_device") == std::string::npos ||
      client_extensions.find("EGL_EXT_device_enumeration") ==
          std::string::npos) {
    return Ok{};
  }

  if (egl.eglGetPlatformDisplayEXT == nullptr) {
    return gfxstream::unexpected(
        "EGL_EXT_platform_base available but failed to load "
        "eglGetPlatformDisplayEXT().");
  }

  if (egl.eglQueryDevicesEXT == nullptr) {
    return gfxstream::unexpected(
        "EGL_EXT_device_enumeration available but failed to load "
        "eglQueryDevicesEXT().");
  }

  constexpr const size_t kMaxDevices = 8;
  std::vector<EGLDeviceEXT> devices(kMaxDevices, EGL_NO_DEVICE_EXT);

  EGLint num_devices = 0;
  EGLBoolean result = egl.eglQueryDevicesEXT(
      static_cast<EGLint>(devices.size()), devices.data(), &num_devices);
  if (result != EGL_TRUE) {
    return gfxstream::unexpected("Failed to query devices: " +
                                 std::to_string(egl.eglGetError()));
  }

  for (EGLint i = 0; i < num_devices; i++) {
    PlatformAvailability* device_platform_availability =
        egl_availability->add_device_platform_availabilities();

    EGLDisplay display = egl.eglGetPlatformDisplayEXT(
        EGL_PLATFORM_DEVICE_EXT, reinterpret_cast<void*>(devices[i]), NULL);
    if (display == EGL_NO_DISPLAY) {
      device_platform_availability->add_errors(
          "Failed to complete explicit device platform availability check: "
          "eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT) returned "
          "EGL_NO_DISPLAY.");
      continue;
    }

    auto result = PopulatePlatformAvailability(egl, display,
                                               *device_platform_availability);
    if (!result.ok()) {
      device_platform_availability->add_errors(
          "Failed to complete explicit device platform availability check: " +
          result.error());
      continue;
    }
  }

  return Ok{};
}

}  // namespace

gfxstream::expected<Ok, std::string> PopulateEglAndGlesAvailability(
    ::gfxstream::proto::GraphicsAvailability* availability) {
  auto egl = GFXSTREAM_EXPECT(Egl::Load());

  EglAvailability* egl_availability = availability->mutable_egl();

  const std::string client_extensions =
      egl.eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
  if (client_extensions.empty()) {
    return gfxstream::unexpected("Failed to query EGL client extensions.");
  }
  egl_availability->set_client_extensions(client_extensions);

  {
    auto result = PopulateDefaultPlatformAvailability(egl, egl_availability);
    if (!result.ok()) {
      egl_availability->add_errors(
          "Failed to populate default platform availability: " +
          result.error());
    }
  }
  {
    auto result = PopulateExplicitPlatformAvailabilities(egl, client_extensions,
                                                         egl_availability);
    if (!result.ok()) {
      egl_availability->add_errors(
          "Failed to populate explicit platform availabilities: " +
          result.error());
    }
  }
  {
    auto result = PopulateExplicitDevicePlatformAvailabilities(
        egl, client_extensions, egl_availability);
    if (!result.ok()) {
      egl_availability->add_errors(
          "Failed to populate explicit device platform availabilities: " +
          result.error());
    }
  }

  return Ok{};
}

}  // namespace gfxstream
