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

#include "cuttlefish/host/graphics_detector/egl.h"

#include "GLES/gl.h"

namespace gfxstream {
namespace {

constexpr const char kEglLib[] = "libEGL.so";
constexpr const char kEglLibAlt[] = "libEGL.so.1";

gfxstream::expected<Lib, std::string> LoadEglLib() {
  for (const auto* possible_name : {kEglLib, kEglLibAlt}) {
    auto result = Lib::Load(possible_name);
    if (result.ok()) {
      return std::move(result.value());
    }
  }
  return gfxstream::unexpected("Failed to load libEGL.");
}

}  // namespace

/*static*/
gfxstream::expected<Egl, std::string> Egl::Load() {
  Egl egl;
  egl.mLib = GFXSTREAM_EXPECT(LoadEglLib());

#define LOAD_EGL_FUNCTION_POINTER(return_type, function_name, signature)       \
  egl.function_name = reinterpret_cast<return_type(GL_APIENTRY*) signature>(   \
      egl.mLib.GetSymbol(#function_name));                                     \
  if (egl.function_name == nullptr) {                                          \
    egl.function_name = reinterpret_cast<return_type(GL_APIENTRY*) signature>( \
        egl.eglGetProcAddress(#function_name));                                \
  }

  FOR_EACH_EGL_FUNCTION(LOAD_EGL_FUNCTION_POINTER);

  GFXSTREAM_EXPECT(egl.Init());

  return std::move(egl);
}

gfxstream::expected<Ok, std::string> Egl::Init() {
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display == EGL_NO_DISPLAY) {
    return gfxstream::unexpected("Failed to get default display");
  }

  EGLint clientVersionMajor = 0;
  EGLint clientVersionMinor = 0;
  if (eglInitialize(display, &clientVersionMajor, &clientVersionMinor) !=
      EGL_TRUE) {
    return gfxstream::unexpected("Failed to initialize display.");
  }

  const std::string vendorString = eglQueryString(display, EGL_VENDOR);
  if (vendorString.empty()) {
    return gfxstream::unexpected("Failed to query vendor.");
  }

  const std::string extensionsString = eglQueryString(display, EGL_EXTENSIONS);
  if (extensionsString.empty()) {
    return gfxstream::unexpected("Failed to query extensions.");
  }

  if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
    return gfxstream::unexpected("Failed to bind GLES API.");
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
  EGLint numConfigs = 0;
  if (eglChooseConfig(display, attribs, &config, 1, &numConfigs) != EGL_TRUE) {
    return gfxstream::unexpected("Failed to find matching framebuffer config.");
  }

  const EGLint pbufferAttribs[] = {
      // clang-format off
        EGL_WIDTH,  720,
        EGL_HEIGHT, 720,
        EGL_NONE,
      // clang-format on
  };

  EGLSurface primarySurface =
      eglCreatePbufferSurface(display, config, pbufferAttribs);
  if (primarySurface == EGL_NO_SURFACE) {
    return gfxstream::unexpected("Failed to create EGL surface.");
  }

  const EGLint contextAttribs[] = {
      // clang-format off
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
      // clang-format on
  };

  EGLContext primaryContext =
      eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
  if (primaryContext == EGL_NO_CONTEXT) {
    return gfxstream::unexpected("Failed to create EGL context.");
  }

  if (eglMakeCurrent(display, primarySurface, primarySurface, primaryContext) ==
      EGL_FALSE) {
    return gfxstream::unexpected(
        "Failed to make primary EGL context/surface current.");
  }

  return {};
}

}  // namespace gfxstream