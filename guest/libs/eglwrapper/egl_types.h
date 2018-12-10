/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __egl_types_h
#define __egl_types_h

typedef const char* EGLconstcharptr;

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <mutex>

extern std::mutex g_context_mutex;

struct egl_wrapper_context_t;
extern egl_wrapper_context_t* (*getEGLContext)(void);

#define GET_CONTEXT \
	std::lock_guard<std::mutex> lock(g_context_mutex); \
	egl_wrapper_context_t *ctx = getEGLContext()

#endif
