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

#include "gles3_wrapper_context.h"
#undef GET_CONTEXT

#include <dlfcn.h>

gles3_wrapper_context_t* (*getGLES3Context)(void) = NULL;

static gles3_wrapper_context_t g_gles3_wrapper_context;

static gles3_wrapper_context_t *gles3(void) {
	return &g_gles3_wrapper_context;
}

void gles3_wrapper_context_t::setContextAccessor(gles3_wrapper_context_t* (*f)(void)) {
	getGLES3Context = f;
}

std::mutex g_context_mutex;

static void *getProc(const char *name, void *userData) {
	return dlsym(userData, name);
}

__attribute__((constructor)) void setup() {
	void *gles3_handle = dlopen("/system/vendor/lib/egl/libGLESv2_swiftshader.so", RTLD_NOW);
	g_gles3_wrapper_context.initDispatchByName(getProc, gles3_handle);
	g_gles3_wrapper_context.setContextAccessor(gles3);
}
