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

#include <android-base/logging.h>
#include <gflags/gflags.h>
#include <keymaster/android_keymaster.h>
#include <keymaster/contexts/pure_soft_keymaster_context.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/security/keymaster_channel.h"
#include "host/commands/secure_env/keymaster_responder.h"

// Copied from AndroidKeymaster4Device
constexpr size_t kOperationTableSize = 16;

DEFINE_int32(keymaster_fd, -1, "A file descriptor for keymaster communication");

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  keymaster::PureSoftKeymasterContext keymaster_context{
      KM_SECURITY_LEVEL_SOFTWARE};
  keymaster::AndroidKeymaster keymaster{&keymaster_context, kOperationTableSize};

  CHECK(FLAGS_keymaster_fd != -1)
      << "TODO(schuffelen): Add keymaster_fd alternative";
  auto server = cvd::SharedFD::Dup(FLAGS_keymaster_fd);
  CHECK(server->IsOpen()) << "Could not dup server fd: " << server->StrError();
  close(FLAGS_keymaster_fd);
  auto conn = cvd::SharedFD::Accept(*server);
  CHECK(conn->IsOpen()) << "Unable to open connection: " << conn->StrError();
  cvd::KeymasterChannel keymaster_channel(conn);

  KeymasterResponder keymaster_responder(&keymaster_channel, &keymaster);

  // TODO(schuffelen): Do this in a thread when adding other HALs
  while (keymaster_responder.ProcessMessage()) {
  }
}
