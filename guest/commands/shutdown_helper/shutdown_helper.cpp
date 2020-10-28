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
#include <cutils/android_reboot.h>

#include <common/libs/fs/shared_buf.h>
#include <common/libs/fs/shared_fd.h>

using cuttlefish::SharedFD;

const char DEVICE[] = "/dev/hvc5";

int main(int, char** argv) {
  android::base::InitLogging(argv, android::base::KernelLogger);

  auto fd = SharedFD::Open(DEVICE, O_RDWR);
  CHECK(fd->IsOpen()) << "error connecting to host " << fd->StrError();
  CHECK(fd->SetTerminalRaw() >= 0) << "Could not make " << DEVICE
                                   << " a raw terminal: " << fd->StrError();

  std::string initial = "ready";
  int written = cuttlefish::WriteAll(fd, initial);
  CHECK(written == initial.size()) << "Error in writing data: sent " << written
                                   << " bytes and error: " << fd->StrError();

  std::string input = "________";
  int read = cuttlefish::ReadExact(fd, &input);
  CHECK(read == input.size()) << "Error in reading data: received " << read
                              << " bytes and error: " << fd->StrError();

  // TODO(schuffelen): Wait for a boot event rather than depending on crosvm
  // exiting with ANDROID_RB_RESTART2.
  // ANDROID_RB_RESTART2 works better than ANDROID_RB_POWEROFF: for some
  // reason, the shutdown command leaves crosvm in a zombie state.
  android_reboot(ANDROID_RB_RESTART2, 0, nullptr);
}
