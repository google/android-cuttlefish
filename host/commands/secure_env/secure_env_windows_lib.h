//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <windows.h>

#if !defined(SECURE_ENV_DLL)
#define SECURE_ENV_DLL_SYMBOL
#elif defined(SECURE_ENV_BUILD_DLL)
#define SECURE_ENV_DLL_SYMBOL __declspec(dllexport)
#else
#define SECURE_ENV_DLL_SYMBOL __declspec(dllimport)
#endif

namespace secure_env {
extern "C" {
/* Starts and runs remote keymaster and gatekeeper in separate threads.
 * All cryptography is performed in software. Returns on failure, or once
 * the connections are dropped on success.
 *
 */
SECURE_ENV_DLL_SYMBOL bool StartSecureEnv(const char* keymaster_pipe,
                                          const char* gatekeeper_pipe,
                                          bool use_tpm);

/* Starts and runs remote keymaster and gatekeeper using handles to preexisting
 * async named pipes. Returns on failure, or once the connections are dropped on
 * success.
 */
SECURE_ENV_DLL_SYMBOL bool StartSecureEnvWithHandles(
    HANDLE keymaster_pipe_handle, HANDLE gatekeeper_pipe_handle, bool use_tpm);
}

}  // namespace secure_env
