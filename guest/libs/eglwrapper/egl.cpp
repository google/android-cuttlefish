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

#include "egl_wrapper_context.h"
#undef GET_CONTEXT

#include <mutex>
#include <dlfcn.h>

egl_wrapper_context_t* (*getEGLContext)(void) = NULL;

static egl_wrapper_context_t g_egl_wrapper_context;

static egl_wrapper_context_t *egl_context(void) {
	return &g_egl_wrapper_context;
}

void egl_wrapper_context_t::setContextAccessor(egl_wrapper_context_t* (*f)(void)) {
	getEGLContext = f;
}

static std::mutex context_mutex;
thread_local std::unique_lock<std::mutex> g_current_txn(
		context_mutex, std::defer_lock);

ScopedTxn::ScopedTxn() {
	g_current_txn.lock();
}

ScopedTxn::~ScopedTxn() {
	if (g_current_txn.owns_lock()) {
	  g_current_txn.unlock();
	}
}


static int nativeWindowHook(NativeWindowRequest* request) {
	static t_nativeWindowFunction next = nullptr;
	if (request->command == RegisterInnerFunction) {
		next = request->inner_function;
		return 0;
	}
	if (!next) {
		return 0;
	}
	if (g_current_txn.owns_lock()) {
	  g_current_txn.unlock();
	}
	return next(request);
}

static void *getProc(const char *name, void *userData) {
	return dlsym(userData, name);
}

__attribute__((constructor)) void setup() {
	void *egl_handle = dlopen("/vendor/lib/gl_impl/swiftshader/libEGL_swiftshader.so", RTLD_NOW);
	g_egl_wrapper_context.initDispatchByName(getProc, egl_handle);
	g_egl_wrapper_context.setContextAccessor(egl_context);
	t_nativeWindowFunction (*hook)(t_nativeWindowFunction) =
		(t_nativeWindowFunction (*)(t_nativeWindowFunction))
		g_egl_wrapper_context.eglGetProcAddress("eglHookNativeWindow");
	if (hook) {
		hook(nativeWindowHook);
	}
}
