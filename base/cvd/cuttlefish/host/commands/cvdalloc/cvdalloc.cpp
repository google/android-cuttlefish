/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <string.h>
#include <unistd.h>

#include <android-base/logging.h>
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include "allocd/alloc_utils.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"

ABSL_FLAG(int, id, 0, "Id");
ABSL_FLAG(int, teardown_event_socket, 0, "Socket");
ABSL_FLAG(int, allocate_event_socket, 0, "Socket");

static void usage() {
  LOG(ERROR) << "cvdalloc --id=id --allocate_event_socket=fd1 "
                "--teardown_event_socket=fd2";
  LOG(ERROR) << "Should only be invoked from run_cvd.";
}

namespace cuttlefish {

Result<int> CvdallocMain(int argc, char *argv[]) {
  std::vector<char *> args = absl::ParseCommandLine(argc, argv);

  if (absl::GetFlag(FLAGS_id) == 0 ||
      absl::GetFlag(FLAGS_teardown_event_socket) == 0 ||
      absl::GetFlag(FLAGS_allocate_event_socket) == 0) {
    usage();
    return 1;
  }

  /*
   * Explicit setuid calls seem to be required.
   *
   * This likely has something to do with invoking external commands,
   * but it isn't clear why an explicit setuid(0) is necessary.
   * It's possible a Linux kernel bug around permissions checking on tap
   * devices may be the culprit, which we can't control.
   *
   * TODO: We should just use capabilities on Linux and rely on setuid as
   * a fallback for other platforms.
   *
   * Save our current uid, so we can restore it to drop privileges later.
   */
  uid_t orig = getuid();

  int id = absl::GetFlag(FLAGS_id);

  pid_t pid = fork();
  if (pid < 0) return -1;

  if (pid > 0) {
    int r = setuid(0);
    if (r == -1) {
      LOG(ERROR) << "Couldn't setuid root: " << strerror(errno);
      return 1;
    }

    LOG(INFO) << "cvdalloc: allocating network resources";

    CreateBridge("cvd-br");
    CreateMobileIface(absl::StrFormat("cvd-mtap-%02d", id), id, kMobileIp);
    CreateMobileIface(absl::StrFormat("cvd-wtap-%02d", id), id, kWirelessIp);
    CreateEthernetIface(absl::StrFormat("cvd-etap-%02d", id), "cvd-br", true,
                        true, false);

    r = setuid(orig);
    if (r == -1) {
      LOG(WARNING) << "cvdalloc: couldn't drop privileges";
      return 1;
    }

    auto sock = SharedFD::Dup(absl::GetFlag(FLAGS_allocate_event_socket));
    if (!sock->IsOpen()) {
      LOG(ERROR) << "cvdalloc: allocate socket is closed: " << sock->StrError();
      return 1;
    }
    sock->Shutdown(SHUT_RDWR);

    return 0;
  }

  LOG(INFO) << "cvdalloc (teardown): waiting to teardown";

  auto sock = SharedFD::Dup(absl::GetFlag(FLAGS_teardown_event_socket));
  if (!sock->IsOpen()) {
    LOG(ERROR) << "cvdalloc (teardown): teardown socket is closed: "
               << sock->StrError();
    return 1;
  }
  int i;
  if (sock->Read(&i, sizeof(i)) < 0) {
    sock->Close();
    return 1;
  }

  int r = setuid(0);
  if (r == -1) {
    LOG(ERROR) << "Couldn't setuid root: " << strerror(errno);
    return 1;
  }

  LOG(INFO) << "cvdalloc (teardown): tearing down resources";

  DestroyMobileIface(absl::StrFormat("cvd-mtap-%02d", id), id, kMobileIp);
  DestroyMobileIface(absl::StrFormat("cvd-wtap-%02d", id), id, kWirelessIp);
  DestroyEthernetIface(absl::StrFormat("cvd-etap-%02d", id), true, true, false);
  DestroyBridge("cvd-br");

  r = setuid(orig);
  if (r == -1) {
    LOG(WARNING) << "cvdalloc (teardown): couldn't drop privileges: "
                 << strerror(errno);
    return 1;
  }

  return 0;
}

}  // namespace cuttlefish

int main(int argc, char *argv[]) {
  auto res = cuttlefish::CvdallocMain(argc, argv);
  if (res.ok()) {
    return *res;
  }

  LOG(ERROR) << "cvdalloc failed: \n" << res.error().FormatForEnv();
  abort();
}
