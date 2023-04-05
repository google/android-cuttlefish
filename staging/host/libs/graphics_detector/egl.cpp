/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "host/libs/graphics_detector/egl.h"

#include <GLES/gl.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

namespace cuttlefish {
namespace {

constexpr const char kEglLib[] = "libEGL.so";
constexpr const char kEglLibAlt[] = "libEGL.so.1";

std::optional<Lib> LoadEglLib() {
  for (const auto* possible_name : {kEglLib, kEglLibAlt}) {
    auto lib_opt = Lib::Load(possible_name);
    if (!lib_opt) {
      LOG(VERBOSE) << "Failed to load " << possible_name;
    } else {
      LOG(VERBOSE) << "Loaded " << possible_name;
      return std::move(lib_opt);
    }
  }
  return std::nullopt;
}

}  // namespace

/*static*/
std::optional<Egl> Egl::Load() {
  auto lib_opt = LoadEglLib();
  if (!lib_opt) {
    return std::nullopt;
  }

  Egl egl;
  egl.lib_ = std::move(*lib_opt);

#define LOAD_EGL_FUNCTION_POINTER(return_type, function_name, signature)       \
  egl.function_name = reinterpret_cast<return_type(GL_APIENTRY*) signature>(   \
      egl.lib_.GetSymbol(#function_name));                                     \
  if (egl.function_name == nullptr) {                                          \
    egl.function_name = reinterpret_cast<return_type(GL_APIENTRY*) signature>( \
        egl.eglGetProcAddress(#function_name));                                \
  }                                                                            \
  if (egl.function_name == nullptr) {                                          \
    LOG(VERBOSE) << "Failed to load EGL function: " << #function_name;         \
  } else {                                                                     \
    LOG(VERBOSE) << "Loaded EGL function: " << #function_name;                 \
  }

  FOR_EACH_EGL_FUNCTION(LOAD_EGL_FUNCTION_POINTER);

  egl.Init();

  return std::move(egl);
}

void Egl::Init() {
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display == EGL_NO_DISPLAY) {
    LOG(FATAL) << "Failed to get default display";
  }

  EGLint client_version_major = 0;
  EGLint client_version_minor = 0;
  if (eglInitialize(display, &client_version_major, &client_version_minor) !=
      EGL_TRUE) {
    LOG(FATAL) << "Failed to initialize display.";
    return;
  }
  LOG(VERBOSE) << "Found EGL client version " << client_version_major << "."
               << client_version_minor;

  const std::string vendor_string = eglQueryString(display, EGL_VENDOR);
  if (vendor_string.empty()) {
    LOG(FATAL) << "Failed to query vendor.";
    return;
  }
  LOG(VERBOSE) << "Found EGL vendor: " << vendor_string;

  const std::string extensions_string = eglQueryString(display, EGL_EXTENSIONS);
  if (extensions_string.empty()) {
    LOG(FATAL) << "Failed to query extensions.";
    return;
  }
  LOG(VERBOSE) << "Found EGL extensions: " << extensions_string;

  if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
    LOG(FATAL) << "Failed to bind GLES API.";
    return;
  }

  const EGLint attribs[] = {
      // clang-format off
    EGL_SURFACE_TYPE,     EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE,  EGL_OPENGL_ES3_BIT,
    EGL_RED_SIZE,         8,
    EGL_GREEN_SIZE,       8,
    EGL_BLUE_SIZE,        8,
    EGL_ALPHA_SIZE,       8,
    EGL_NONE,
      // clang-format on
  };

  EGLConfig config;
  EGLint num_configs = 0;
  if (eglChooseConfig(display, attribs, &config, 1, &num_configs) != EGL_TRUE) {
    LOG(FATAL) << "Failed to find matching framebuffer config.";
    return;
  }
  LOG(VERBOSE) << "Found matching framebuffer config.";

  const EGLint pbuffer_attribs[] = {
      // clang-format off
    EGL_WIDTH,  720,
    EGL_HEIGHT, 720,
    EGL_NONE,
      // clang-format on
  };

  EGLSurface primary_surface =
      eglCreatePbufferSurface(display, config, pbuffer_attribs);
  if (primary_surface == EGL_NO_SURFACE) {
    LOG(FATAL) << "Failed to create EGL surface.";
    return;
  }

  const EGLint context_attribs[] = {
      // clang-format off
    EGL_CONTEXT_CLIENT_VERSION, 3,
    EGL_NONE
      // clang-format on
  };

  EGLContext primary_context =
      eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
  if (primary_context == EGL_NO_CONTEXT) {
    LOG(FATAL) << "Failed to create EGL context.";
    return;
  }

  if (eglMakeCurrent(display, primary_surface, primary_surface,
                     primary_context) == EGL_FALSE) {
    LOG(FATAL) << "Failed to make primary EGL context/surface current.";
    return;
  }
}

}  // namespace cuttlefish