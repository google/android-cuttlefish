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

#include <string_view>

#include <android-base/logging.h>
#include <android-base/macros.h>
#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include "allocd/alloc_utils.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/fs/shared_select.h"
#include "cuttlefish/common/libs/posix/strerror.h"
#include "cuttlefish/host/commands/cvdalloc/interface.h"
#include "cuttlefish/host/commands/cvdalloc/privilege.h"
#include "cuttlefish/host/commands/cvdalloc/sem.h"

ABSL_FLAG(int, id, 0, "Id");
ABSL_FLAG(int, socket, 0, "Socket");

namespace cuttlefish {
namespace {

void Usage() {
  LOG(ERROR) << "cvdalloc --id=id --socket=fd ";
  LOG(ERROR) << "Should only be invoked from run_cvd.";
}

Result<void> Allocate(int id, std::string_view ethernet_bridge_name,
                      std::string_view wireless_bridge_name) {
  LOG(INFO) << "cvdalloc: allocating network resources";

  CF_EXPECT(CreateMobileIface(CvdallocInterfaceName("mtap", id), id,
                              kCvdallocMobileIpPrefix));
  CF_EXPECT(CreateEthernetBridgeIface(wireless_bridge_name,
                                      kCvdallocWirelessIpPrefix));
  CF_EXPECT(CreateEthernetIface(CvdallocInterfaceName("wtap", id),
                                wireless_bridge_name, true, true, false));
  CF_EXPECT(CreateMobileIface(CvdallocInterfaceName("wifiap", id), id,
                              kCvdallocWirelessApIpPrefix));
  CF_EXPECT(CreateEthernetBridgeIface(ethernet_bridge_name,
                                      kCvdallocEthernetIpPrefix));
  CF_EXPECT(CreateEthernetIface(CvdallocInterfaceName("etap", id),
                                ethernet_bridge_name, true, true, false));

  return {};
}

Result<void> Teardown(int id, std::string_view ethernet_bridge_name,
                      std::string_view wireless_bridge_name) {
  LOG(INFO) << "cvdalloc: tearing down resources";

  DestroyMobileIface(CvdallocInterfaceName("mtap", id), id,
                     kCvdallocMobileIpPrefix);
  DestroyMobileIface(CvdallocInterfaceName("wtap", id), id,
                     kCvdallocWirelessIpPrefix);
  DestroyMobileIface(CvdallocInterfaceName("wifiap", id), id,
                     kCvdallocWirelessApIpPrefix);
  DestroyEthernetIface(CvdallocInterfaceName("etap", id), true, true, false);
  DestroyBridge(ethernet_bridge_name);
  DestroyBridge(wireless_bridge_name);

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
    return CF_ERRNO("close: " << StrError(errno));
  }

  absl::Cleanup shutdown = [sock]() {
    sock->Shutdown(SHUT_RDWR);
  };

  /*
   * Save our current uid, so we can restore it to drop privileges later.
   */
  uid_t orig = getuid();

  absl::Cleanup drop_privileges = [orig]() {
    int r = DropPrivileges(orig);
    if (r == -1) {
      LOG(ERROR) << "cvdalloc: couldn't drop privileges: " << StrError(errno);
    }
  };

  r = BeginElevatedPrivileges();
  if (r == -1) {
    return CF_ERRF("Couldn't elevate permissions: {}", StrError(errno));
  }

  absl::Cleanup teardown = [id]() {
    LOG(INFO) << "cvdalloc: teardown started";
    Teardown(id, kCvdallocEthernetBridgeName, kCvdallocWirelessBridgeName);
  };

  CF_EXPECT(Allocate(id, kCvdallocEthernetBridgeName, kCvdallocWirelessBridgeName));
  CF_EXPECT(cvdalloc::Post(sock));

  LOG(INFO) << "cvdalloc: waiting to teardown";

  CF_EXPECT(cvdalloc::Wait(sock, cvdalloc::kSemNoTimeout));
  std::move(teardown).Invoke();
  CF_EXPECT(cvdalloc::Post(sock));

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
