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

#include "host/libs/graphics_detector/graphics_detector_gl.h"

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "host/libs/graphics_detector/egl.h"
#include "host/libs/graphics_detector/gles.h"
#include "host/libs/graphics_detector/subprocess.h"

namespace cuttlefish {
namespace {

constexpr const char kSurfacelessContextExt[] = "EGL_KHR_surfaceless_context";

class Closer {
 public:
  Closer(std::function<void()> on_close) : on_close_(std::move(on_close)) {}
  ~Closer() { on_close_(); }

 private:
  std::function<void()> on_close_;
};

void PopulateEglAndGlesAvailabilityImpl(GraphicsAvailability* availability) {
  auto egl = Egl::Load();
  if (!egl) {
    LOG(VERBOSE) << "Failed to load EGL library.";
    return;
  }
  LOG(VERBOSE) << "Loaded EGL library.";
  availability->has_egl = true;

  EGLDisplay display = egl->eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display != EGL_NO_DISPLAY) {
    LOG(VERBOSE) << "Found default display.";
  } else {
    LOG(VERBOSE) << "Failed to get default display. " << egl->eglGetError()
                 << ". Attempting to get surfaceless display via "
                 << "eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA)";

    if (egl->eglGetPlatformDisplayEXT == nullptr) {
      LOG(VERBOSE) << "Failed to find function eglGetPlatformDisplayEXT";
    } else {
      display = egl->eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA,
                                              EGL_DEFAULT_DISPLAY, NULL);
    }
  }

  if (display == EGL_NO_DISPLAY) {
    LOG(VERBOSE) << "Failed to find display.";
    return;
  }

  EGLint client_version_major = 0;
  EGLint client_version_minor = 0;
  if (egl->eglInitialize(display, &client_version_major,
                         &client_version_minor) != EGL_TRUE) {
    LOG(VERBOSE) << "Failed to initialize display.";
    return;
  }
  LOG(VERBOSE) << "Initialized display.";

  const std::string version_string = egl->eglQueryString(display, EGL_VERSION);
  if (version_string.empty()) {
    LOG(VERBOSE) << "Failed to query client version.";
    return;
  }
  LOG(VERBOSE) << "Found version: " << version_string;
  availability->egl_version = version_string;

  const std::string vendor_string = egl->eglQueryString(display, EGL_VENDOR);
  if (vendor_string.empty()) {
    LOG(VERBOSE) << "Failed to query vendor.";
    return;
  }
  LOG(VERBOSE) << "Found vendor: " << vendor_string;
  availability->egl_vendor = vendor_string;

  const std::string extensions_string =
      egl->eglQueryString(display, EGL_EXTENSIONS);
  if (extensions_string.empty()) {
    LOG(VERBOSE) << "Failed to query extensions.";
    return;
  }
  LOG(VERBOSE) << "Found extensions: " << extensions_string;
  availability->egl_extensions = extensions_string;

  if (extensions_string.find(kSurfacelessContextExt) == std::string::npos) {
    LOG(VERBOSE) << "Failed to find extension EGL_KHR_surfaceless_context.";
    return;
  }

  const std::string display_apis_string =
      egl->eglQueryString(display, EGL_CLIENT_APIS);
  if (display_apis_string.empty()) {
    LOG(VERBOSE) << "Failed to query display apis.";
    return;
  }
  LOG(VERBOSE) << "Found display apis: " << display_apis_string;

  if (egl->eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
    LOG(VERBOSE) << "Failed to bind GLES API.";
    return;
  }
  LOG(VERBOSE) << "Bound GLES API.";

  const EGLint framebuffer_config_attributes[] = {
      EGL_SURFACE_TYPE,
      EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE,
      EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE,
      1,
      EGL_GREEN_SIZE,
      1,
      EGL_BLUE_SIZE,
      1,
      EGL_ALPHA_SIZE,
      0,
      EGL_NONE,
  };

  EGLConfig framebuffer_config;
  EGLint num_framebuffer_configs = 0;
  if (egl->eglChooseConfig(display, framebuffer_config_attributes,
                           &framebuffer_config, 1,
                           &num_framebuffer_configs) != EGL_TRUE) {
    LOG(VERBOSE) << "Failed to find matching framebuffer config.";
    return;
  }
  LOG(VERBOSE) << "Found matching framebuffer config.";

  const EGLint gles2_context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                             EGL_NONE};
  EGLContext gles2_context = egl->eglCreateContext(
      display, framebuffer_config, EGL_NO_CONTEXT, gles2_context_attributes);
  if (gles2_context == EGL_NO_CONTEXT) {
    LOG(VERBOSE) << "Failed to create EGL context.";
  } else {
    LOG(VERBOSE) << "Created EGL context.";
    Closer context_closer(
        [&]() { egl->eglDestroyContext(display, gles2_context); });

    if (egl->eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                            gles2_context) != EGL_TRUE) {
      LOG(VERBOSE) << "Failed to make GLES2 context current.";
      return;
    }
    LOG(VERBOSE) << "Make GLES2 context current.";
    availability->can_init_gles2_on_egl_surfaceless = true;

    auto gles = Gles::LoadFromEgl(&*egl);
    if (!gles) {
      LOG(VERBOSE) << "Failed to load GLES library.";
      return;
    }

    const GLubyte* gles2_vendor = gles->glGetString(GL_VENDOR);
    if (gles2_vendor == nullptr) {
      LOG(VERBOSE) << "Failed to query GLES2 vendor.";
      return;
    }
    const std::string gles2_vendor_string((const char*)gles2_vendor);
    LOG(VERBOSE) << "Found GLES2 vendor: " << gles2_vendor_string;
    availability->gles2_vendor = gles2_vendor_string;

    const GLubyte* gles2_version = gles->glGetString(GL_VERSION);
    if (gles2_version == nullptr) {
      LOG(VERBOSE) << "Failed to query GLES2 vendor.";
      return;
    }
    const std::string gles2_version_string((const char*)gles2_version);
    LOG(VERBOSE) << "Found GLES2 version: " << gles2_version_string;
    availability->gles2_version = gles2_version_string;

    const GLubyte* gles2_renderer = gles->glGetString(GL_RENDERER);
    if (gles2_renderer == nullptr) {
      LOG(VERBOSE) << "Failed to query GLES2 renderer.";
      return;
    }
    const std::string gles2_renderer_string((const char*)gles2_renderer);
    LOG(VERBOSE) << "Found GLES2 renderer: " << gles2_renderer_string;
    availability->gles2_renderer = gles2_renderer_string;

    const GLubyte* gles2_extensions = gles->glGetString(GL_EXTENSIONS);
    if (gles2_extensions == nullptr) {
      LOG(VERBOSE) << "Failed to query GLES2 extensions.";
      return;
    }
    const std::string gles2_extensions_string((const char*)gles2_extensions);
    LOG(VERBOSE) << "Found GLES2 extensions: " << gles2_extensions_string;
    availability->gles2_extensions = gles2_extensions_string;
  }

  const EGLint gles3_context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3,
                                             EGL_NONE};
  EGLContext gles3_context = egl->eglCreateContext(
      display, framebuffer_config, EGL_NO_CONTEXT, gles3_context_attributes);
  if (gles3_context == EGL_NO_CONTEXT) {
    LOG(VERBOSE) << "Failed to create GLES3 context.";
  } else {
    LOG(VERBOSE) << "Created GLES3 context.";
    Closer context_closer(
        [&]() { egl->eglDestroyContext(display, gles3_context); });

    if (egl->eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                            gles3_context) != EGL_TRUE) {
      LOG(VERBOSE) << "Failed to make GLES3 context current.";
      return;
    }
    LOG(VERBOSE) << "Make GLES3 context current.";
    availability->can_init_gles3_on_egl_surfaceless = true;

    auto gles = Gles::LoadFromEgl(&*egl);
    if (!gles) {
      LOG(VERBOSE) << "Failed to load GLES library.";
      return;
    }

    const GLubyte* gles3_vendor = gles->glGetString(GL_VENDOR);
    if (gles3_vendor == nullptr) {
      LOG(VERBOSE) << "Failed to query GLES3 vendor.";
      return;
    }
    const std::string gles3_vendor_string((const char*)gles3_vendor);
    LOG(VERBOSE) << "Found GLES3 vendor: " << gles3_vendor_string;
    availability->gles3_vendor = gles3_vendor_string;

    const GLubyte* gles3_version = gles->glGetString(GL_VERSION);
    if (gles3_version == nullptr) {
      LOG(VERBOSE) << "Failed to query GLES2 vendor.";
      return;
    }
    const std::string gles3_version_string((const char*)gles3_version);
    LOG(VERBOSE) << "Found GLES3 version: " << gles3_version_string;
    availability->gles3_version = gles3_version_string;

    const GLubyte* gles3_renderer = gles->glGetString(GL_RENDERER);
    if (gles3_renderer == nullptr) {
      LOG(VERBOSE) << "Failed to query GLES3 renderer.";
      return;
    }
    const std::string gles3_renderer_string((const char*)gles3_renderer);
    LOG(VERBOSE) << "Found GLES3 renderer: " << gles3_renderer_string;
    availability->gles3_renderer = gles3_renderer_string;

    const GLubyte* gles3_extensions = gles->glGetString(GL_EXTENSIONS);
    if (gles3_extensions == nullptr) {
      LOG(VERBOSE) << "Failed to query GLES3 extensions.";
      return;
    }
    const std::string gles3_extensions_string((const char*)gles3_extensions);
    LOG(VERBOSE) << "Found GLES3 extensions: " << gles3_extensions_string;
    availability->gles3_extensions = gles3_extensions_string;
  }
}

}  // namespace

void PopulateEglAndGlesAvailability(GraphicsAvailability* availability) {
  DoWithSubprocessCheck("PopulateEglAndGlesAvailability", [&]() {
    PopulateEglAndGlesAvailabilityImpl(availability);
  });
}

}  // namespace cuttlefish
