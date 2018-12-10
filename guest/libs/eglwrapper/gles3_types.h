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

#ifndef __gles3_types_h
#define __gles3_types_h

typedef void* GLeglImageOES;
typedef void* GLvoidptr;

#include <GLES3/gl3.h>

#include <mutex>

extern std::mutex g_context_mutex;

struct gles3_wrapper_context_t;
extern gles3_wrapper_context_t* (*getGLES3Context)(void);

#define GET_CONTEXT \
	std::lock_guard<std::mutex> lock(g_context_mutex); \
	gles3_wrapper_context_t *ctx = getGLES3Context()

#endif
