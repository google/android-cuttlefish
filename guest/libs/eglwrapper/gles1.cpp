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

#include "gles1_wrapper_context.h"
#undef GET_CONTEXT

#include <dlfcn.h>

gles1_wrapper_context_t* (*getGLES1Context)(void) = NULL;

static gles1_wrapper_context_t g_gles1_wrapper_context;

static gles1_wrapper_context_t *gles1(void) {
	return &g_gles1_wrapper_context;
}

void gles1_wrapper_context_t::setContextAccessor(gles1_wrapper_context_t* (*f)(void)) {
	getGLES1Context = f;
}

static void *getProc(const char *name, void *userData) {
	return dlsym(userData, name);
}

__attribute__((constructor)) void setup() {
	void *gles1_handle = dlopen("/vendor/lib/gl_impl/swiftshader/libGLESv1_CM_swiftshader.so", RTLD_NOW);
	g_gles1_wrapper_context.initDispatchByName(getProc, gles1_handle);
	g_gles1_wrapper_context.setContextAccessor(gles1);
}
