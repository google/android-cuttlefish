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
#include <android-base/macros.h>
#include <android-base/scopeguard.h>
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include "allocd/alloc_utils.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"

ABSL_FLAG(int, id, 0, "Id");
ABSL_FLAG(int, socket, 0, "Socket");

namespace cuttlefish {
namespace {

void Usage() {
  LOG(ERROR) << "cvdalloc --id=id --socket=fd ";
  LOG(ERROR) << "Should only be invoked from run_cvd.";
}

Result<void> Allocate(int id) {
  LOG(INFO) << "cvdalloc: allocating network resources";

  CreateBridge("cvd-br");
  CF_EXPECT(
      CreateMobileIface(absl::StrFormat("cvd-mtap-%02d", id), id, kMobileIp));
  CF_EXPECT(
      CreateMobileIface(absl::StrFormat("cvd-wtap-%02d", id), id, kWirelessIp));
  CF_EXPECT(CreateEthernetIface(absl::StrFormat("cvd-etap-%02d", id), "cvd-br",
                                true, true, false));
  return {};
}

Result<void> Teardown(int id) {
  LOG(INFO) << "cvdalloc: tearing down resources";

  DestroyMobileIface(absl::StrFormat("cvd-mtap-%02d", id), id, kMobileIp);
  DestroyMobileIface(absl::StrFormat("cvd-wtap-%02d", id), id, kWirelessIp);
  DestroyEthernetIface(absl::StrFormat("cvd-etap-%02d", id), true, true, false);
  DestroyBridge("cvd-br");
  return {};
}

}  // namespace

Result<int> CvdallocMain(int argc, char *argv[]) {
  std::vector<char *> args = absl::ParseCommandLine(argc, argv);

  if (absl::GetFlag(FLAGS_id) == 0 || absl::GetFlag(FLAGS_socket) == 0) {
    Usage();
    /* No need to dump a trace for usage. */
    return 1;
  }

  int id = absl::GetFlag(FLAGS_id);

  auto sock = SharedFD::Dup(absl::GetFlag(FLAGS_socket));
  if (!sock->IsOpen()) {
    return CF_ERRNO("cvdalloc: socket is closed: " << sock->StrError());
  }
  int r = TEMP_FAILURE_RETRY(close(absl::GetFlag(FLAGS_socket)));
  if (r == -1) {
    return CF_ERRNO("close: " << strerror(errno));
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

  auto drop_privileges = android::base::ScopeGuard([orig]() {
    int r = setuid(orig);
    if (r == -1) {
      LOG(ERROR) << "cvdalloc: couldn't drop privileges: " << strerror(errno);
    }
  });

  r = setuid(0);
  if (r == -1) {
    return CF_ERRNO("Couldn't setuid root: " << strerror(errno));
  }

  auto teardown = android::base::ScopeGuard([id, sock]() {
    sock->Shutdown(SHUT_RDWR);

    Teardown(id);
  });

  CF_EXPECT(Allocate(id));

  int i = 0;
  r = sock->Write(&i, sizeof(int));
  if (r == -1) {
    return CF_ERRNO("Write: " << strerror(errno));
  }

  LOG(INFO) << "cvdalloc: waiting to teardown";

  if (sock->Read(&i, sizeof(i)) < 0) {
    return CF_ERRNO("Read: " << strerror(errno));
  }

  /* Teardown invoked by scopeguard above. */

  return 0;
}

}  // namespace cuttlefish

int main(int argc, char *argv[]) {
  auto res = cuttlefish::CvdallocMain(argc, argv);
  if (!res.ok()) {
    LOG(ERROR) << "cvdalloc failed: \n" << res.error().FormatForEnv();
    abort();
  }

  return *res;
}
