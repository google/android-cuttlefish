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

#include <thread>

#include <android-base/logging.h>
#include <gflags/gflags.h>
#include <keymaster/android_keymaster.h>
#include <keymaster/soft_keymaster_logger.h>
#include <keymaster/contexts/pure_soft_keymaster_context.h>
#include <tss2/tss2_esys.h>
#include <tss2/tss2_rc.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/security/gatekeeper_channel.h"
#include "common/libs/security/keymaster_channel.h"
#include "host/commands/secure_env/device_tpm.h"
#include "host/commands/secure_env/fragile_tpm_storage.h"
#include "host/commands/secure_env/gatekeeper_responder.h"
#include "host/commands/secure_env/insecure_fallback_storage.h"
#include "host/commands/secure_env/in_process_tpm.h"
#include "host/commands/secure_env/keymaster_responder.h"
#include "host/commands/secure_env/soft_gatekeeper.h"
#include "host/commands/secure_env/tpm_gatekeeper.h"
#include "host/commands/secure_env/tpm_keymaster_context.h"
#include "host/commands/secure_env/tpm_keymaster_enforcement.h"
#include "host/commands/secure_env/tpm_resource_manager.h"
#include "host/libs/config/logging.h"

// Copied from AndroidKeymaster4Device
constexpr size_t kOperationTableSize = 16;

DEFINE_int32(keymaster_fd_in, -1, "A pipe for keymaster communication");
DEFINE_int32(keymaster_fd_out, -1, "A pipe for keymaster communication");
DEFINE_int32(gatekeeper_fd_in, -1, "A pipe for gatekeeper communication");
DEFINE_int32(gatekeeper_fd_out, -1, "A pipe for gatekeeper communication");

DEFINE_string(tpm_impl,
              "in_memory",
              "The TPM implementation. \"in_memory\" or \"host_device\"");

DEFINE_string(keymint_impl, "tpm",
              "The keymaster implementation. \"tpm\" or \"software\"");

DEFINE_string(gatekeeper_impl, "tpm",
              "The gatekeeper implementation. \"tpm\" or \"software\"");

int main(int argc, char** argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  keymaster::SoftKeymasterLogger km_logger;

  std::unique_ptr<Tpm> tpm;
  if (FLAGS_tpm_impl == "in_memory") {
    tpm.reset(new InProcessTpm());
  } else if (FLAGS_tpm_impl == "host_device") {
    tpm.reset(new DeviceTpm("/dev/tpm0"));
  } else {
    LOG(FATAL) << "Unknown TPM implementation: " << FLAGS_tpm_impl;
  }

  if (tpm->TctiContext() == nullptr) {
    LOG(FATAL) << "Unable to connect to TPM implementation.";
  }

  std::unique_ptr<TpmResourceManager> resource_manager;
  std::unique_ptr<ESYS_CONTEXT, void(*)(ESYS_CONTEXT*)> esys(
      nullptr, [](ESYS_CONTEXT* esys) { Esys_Finalize(&esys); });
  if (FLAGS_keymint_impl == "tpm" || FLAGS_gatekeeper_impl == "tpm") {
    ESYS_CONTEXT* esys_ptr = nullptr;
    auto rc = Esys_Initialize(&esys_ptr, tpm->TctiContext(), nullptr);
    if (rc != TPM2_RC_SUCCESS) {
      LOG(FATAL) << "Could not initialize esys: " << Tss2_RC_Decode(rc)
                 << " (" << rc << ")";
    }
    esys.reset(esys_ptr);
    resource_manager.reset(new TpmResourceManager(esys.get()));
  }

  std::unique_ptr<GatekeeperStorage> secure_storage;
  std::unique_ptr<GatekeeperStorage> insecure_storage;
  std::unique_ptr<gatekeeper::GateKeeper> gatekeeper;
  std::unique_ptr<keymaster::KeymasterEnforcement> keymaster_enforcement;
  if (FLAGS_gatekeeper_impl == "software") {
    gatekeeper.reset(new gatekeeper::SoftGateKeeper);
    keymaster_enforcement.reset(
        new keymaster::SoftKeymasterEnforcement(64, 64));
  } else if (FLAGS_gatekeeper_impl == "tpm") {
    secure_storage.reset(
        new FragileTpmStorage(*resource_manager, "gatekeeper_secure"));
    insecure_storage.reset(
        new InsecureFallbackStorage(*resource_manager, "gatekeeper_insecure"));
    TpmGatekeeper* tpm_gatekeeper =
        new TpmGatekeeper(*resource_manager, *secure_storage, *insecure_storage);
    gatekeeper.reset(tpm_gatekeeper);
    keymaster_enforcement.reset(
        new TpmKeymasterEnforcement(*resource_manager, *tpm_gatekeeper));
  }

  // keymaster::AndroidKeymaster puts the given pointer into a UniquePtr,
  // taking ownership.
  keymaster::KeymasterContext* keymaster_context;
  if (FLAGS_keymint_impl == "software") {
    // TODO: See if this is the right KM version.
    keymaster_context =
        new keymaster::PureSoftKeymasterContext(keymaster::KmVersion::KEYMASTER_4,
                                                KM_SECURITY_LEVEL_SOFTWARE);
  } else if (FLAGS_keymint_impl == "tpm") {
    keymaster_context =
        new TpmKeymasterContext(*resource_manager, *keymaster_enforcement);
  } else {
    LOG(FATAL) << "Unknown keymaster implementation " << FLAGS_keymint_impl;
    return -1;
  }
  keymaster::AndroidKeymaster keymaster{
      keymaster_context, kOperationTableSize,
      keymaster::MessageVersion(keymaster::KmVersion::KEYMINT_1,
                                0 /* km_date */)};

  CHECK(FLAGS_keymaster_fd_in != -1);
  auto keymaster_in = cuttlefish::SharedFD::Dup(FLAGS_keymaster_fd_in);
  CHECK(keymaster_in->IsOpen()) << "Could not dup input fd: "
                                << keymaster_in->StrError();
  close(FLAGS_keymaster_fd_in);

  CHECK(FLAGS_keymaster_fd_out != -1);
  auto keymaster_out = cuttlefish::SharedFD::Dup(FLAGS_keymaster_fd_out);
  CHECK(keymaster_out->IsOpen()) << "Could not dup output fd: "
                                 << keymaster_out->StrError();
  close(FLAGS_keymaster_fd_out);

  CHECK(FLAGS_gatekeeper_fd_in != -1);
  auto gatekeeper_in = cuttlefish::SharedFD::Dup(FLAGS_gatekeeper_fd_in);
  CHECK(gatekeeper_in->IsOpen()) << "Could not dup input fd: "
                                << gatekeeper_in->StrError();
  close(FLAGS_gatekeeper_fd_in);

  CHECK(FLAGS_gatekeeper_fd_out != -1);
  auto gatekeeper_out = cuttlefish::SharedFD::Dup(FLAGS_gatekeeper_fd_out);
  CHECK(gatekeeper_out->IsOpen()) << "Could not dup output fd: "
                                  << keymaster_out->StrError();
  close(FLAGS_gatekeeper_fd_out);

  std::thread keymaster_thread([keymaster_in, keymaster_out, &keymaster]() {
    while (true) {
      cuttlefish::KeymasterChannel keymaster_channel(
          keymaster_in, keymaster_out);

      KeymasterResponder keymaster_responder(keymaster_channel, keymaster);

      while (keymaster_responder.ProcessMessage()) {
      }
    }
  });

  std::thread gatekeeper_thread([gatekeeper_in, gatekeeper_out, &gatekeeper]() {
    while (true) {
      cuttlefish::GatekeeperChannel gatekeeper_channel(
          gatekeeper_in, gatekeeper_out);

      GatekeeperResponder gatekeeper_responder(gatekeeper_channel, *gatekeeper);

      while (gatekeeper_responder.ProcessMessage()) {
      }
    }
  });

  keymaster_thread.join();
  gatekeeper_thread.join();
}
