//
// Copyright (C) 2020 The Android Open Source Project
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

#include <windows.h>

#include "host/commands/secure_env/secure_env_windows_lib.h"

#include <thread>

#include <android-base/logging.h>
#include <keymaster/android_keymaster.h>
#include <keymaster/contexts/pure_soft_keymaster_context.h>
#include "common/libs/security/gatekeeper_channel_windows.h"
#include "common/libs/security/keymaster_channel_windows.h"
#include "host/commands/secure_env/fragile_tpm_storage.h"
#include "host/commands/secure_env/gatekeeper_responder.h"
#include "host/commands/secure_env/insecure_fallback_storage.h"
#include "host/commands/secure_env/keymaster_responder.h"
#include "host/commands/secure_env/soft_gatekeeper.h"
#include "host/commands/secure_env/tpm_gatekeeper.h"
#include "host/commands/secure_env/tpm_keymaster_context.h"
#include "host/commands/secure_env/tpm_keymaster_enforcement.h"
#include "host/commands/secure_env/tpm_resource_manager.h"

namespace secure_env {
namespace {
// Copied from AndroidKeymaster4Device
constexpr size_t kOperationTableSize = 16;

}  // namespace

bool StartSecureEnvWithHandles(HANDLE keymaster_pipe_handle,
                               HANDLE gatekeeper_pipe_handle,
                               bool /*use_tpm*/) {
  // Start threads for gatekeeper and keymaster
  std::thread keymaster_thread([=]() {
    // keymaster::AndroidKeymaster puts keymaster_context into a UniquePtr,
    // taking ownership.
    keymaster::KeymasterContext* keymaster_context =
        new keymaster::PureSoftKeymasterContext(
            keymaster::KmVersion::KEYMASTER_4_1, KM_SECURITY_LEVEL_SOFTWARE);

    // Setup software keymaster
    std::unique_ptr<keymaster::AndroidKeymaster> keymaster_ptr(
        new keymaster::AndroidKeymaster(keymaster_context,
                                        kOperationTableSize));

    std::unique_ptr<cuttlefish::KeymasterWindowsChannel> keymaster_channel =
        cuttlefish::KeymasterWindowsChannel::Create(keymaster_pipe_handle);
    if (!keymaster_channel) {
      return;
    }

    cuttlefish::KeymasterResponder keymaster_responder(*keymaster_channel,
                                                       *keymaster_ptr);

    while (keymaster_responder.ProcessMessage()) {
    }
  });

  std::thread gatekeeper_thread([=]() {
    // Setup software gatekeeper
    std::unique_ptr<gatekeeper::GateKeeper> gatekeeper_ptr(
        new gatekeeper::SoftGateKeeper);

    std::unique_ptr<cuttlefish::GatekeeperWindowsChannel> gatekeeper_channel =
        cuttlefish::GatekeeperWindowsChannel::Create(gatekeeper_pipe_handle);
    if (!gatekeeper_channel) {
      return;
    }

    cuttlefish::GatekeeperResponder gatekeeper_responder(*gatekeeper_channel,
                                                         *gatekeeper_ptr);

    while (gatekeeper_responder.ProcessMessage()) {
    }
  });

  keymaster_thread.join();
  gatekeeper_thread.join();

  return true;
}

bool StartSecureEnv(const char* keymaster_pipe, const char* gatekeeper_pipe,
                    bool use_tpm) {
  // Create the keymaster pipe
  HANDLE keymaster_handle = CreateNamedPipeA(
      keymaster_pipe,
      /* dwOpenMode= */ PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE |
          FILE_FLAG_OVERLAPPED,
      /* dwPipeMode= */ PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
          PIPE_REJECT_REMOTE_CLIENTS,
      /* nMaxInstances= */ 1,
      /* nOutBufferSize= */ 1024,  // The buffer sizes are only advisory.
      /* nInBufferSize= */ 1024,
      /* nDefaultTimeOut= */ 0,
      /* lpSecurityAttributes= */ NULL);
  if (keymaster_handle == INVALID_HANDLE_VALUE) {
    LOG(ERROR) << "Error: Could not create keymaster pipe " << keymaster_pipe
               << ". Got error code " << GetLastError();
    return false;
  }
  LOG(INFO) << "Created keymaster pipe " << keymaster_pipe;

  HANDLE gatekeeper_handle = CreateNamedPipeA(
      gatekeeper_pipe,
      /* dwOpenMode= */ PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE |
          FILE_FLAG_OVERLAPPED,
      /* dwPipeMode= */ PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
          PIPE_REJECT_REMOTE_CLIENTS,
      /* nMaxInstances= */ 1,
      /* nOutBufferSize= */ 1024,  // The buffer sizes are only advisory.
      /* nInBufferSize= */ 1024,
      /* nDefaultTimeOut= */ 0,
      /* lpSecurityAttributes= */ NULL);
  if (gatekeeper_handle == INVALID_HANDLE_VALUE) {
    LOG(ERROR) << "Error: Could not create gatekeeper pipe " << gatekeeper_pipe
               << ". Got error code " << GetLastError();
    return false;
  }
  LOG(INFO) << "Created gatekeeper pipe " << gatekeeper_pipe;

  return StartSecureEnvWithHandles(keymaster_handle, gatekeeper_handle,
                                   use_tpm);
}

}  // namespace secure_env
