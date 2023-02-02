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

#pragma once

// clang-format off
#define FOR_EACH_EGL_FUNCTION(X) \
  X(void*, eglGetProcAddress, (const char* procname)) \
  X(const char*, eglQueryString, (EGLDisplay dpy, EGLint id)) \
  X(EGLDisplay, eglGetPlatformDisplay, (EGLenum platform, void *native_display, const EGLAttrib *attrib_list)) \
  X(EGLDisplay, eglGetPlatformDisplayEXT, (EGLenum platform, void *native_display, const EGLint *attrib_list)) \
  X(EGLBoolean, eglBindAPI, (EGLenum api)) \
  X(EGLBoolean, eglChooseConfig, (EGLDisplay display, EGLint const* attrib_list, EGLConfig* configs, EGLint config_size, EGLint* num_config))  \
  X(EGLContext, eglCreateContext, (EGLDisplay display, EGLConfig config, EGLContext share_context, EGLint const* attrib_list)) \
  X(EGLSurface, eglCreatePbufferSurface, (EGLDisplay display, EGLConfig config, EGLint const* attrib_list)) \
  X(EGLBoolean, eglDestroyContext, (EGLDisplay display, EGLContext context)) \
  X(EGLBoolean, eglDestroySurface, (EGLDisplay display, EGLSurface surface)) \
  X(EGLBoolean, eglGetConfigAttrib, (EGLDisplay display, EGLConfig config, EGLint attribute, EGLint * value)) \
  X(EGLDisplay, eglGetDisplay, (NativeDisplayType native_display)) \
  X(EGLint, eglGetError, (void)) \
  X(EGLBoolean, eglInitialize, (EGLDisplay display, EGLint * major, EGLint * minor)) \
  X(EGLBoolean, eglTerminate, (EGLDisplay display)) \
  X(EGLBoolean, eglMakeCurrent, (EGLDisplay display, EGLSurface draw, EGLSurface read, EGLContext context)) \
  X(EGLBoolean, eglSwapBuffers, (EGLDisplay display, EGLSurface surface)) \
  X(EGLSurface, eglCreateWindowSurface, (EGLDisplay display, EGLConfig config, EGLNativeWindowType native_window, EGLint const* attrib_list)) \
  X(EGLBoolean, eglSwapInterval, (EGLDisplay display, EGLint interval)) \
  X(void, eglSetBlobCacheFuncsANDROID, (EGLDisplay display, EGLSetBlobFuncANDROID set, EGLGetBlobFuncANDROID get)) \
  X(EGLImage, eglCreateImageKHR, (EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list)) \
  X(EGLBoolean, eglDestroyImageKHR, (EGLDisplay dpy, EGLImage image)) \
  X(EGLImage, eglCreateImage, (EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list)) \
  X(EGLBoolean, eglDestroyImage, (EGLDisplay dpy, EGLImage image))

// clang-format on