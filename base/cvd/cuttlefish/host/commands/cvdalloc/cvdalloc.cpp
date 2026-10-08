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

#include <stdlib.h>
#include <unistd.h>

#include <string>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include "allocd/alloc_utils.h"
#include "allocd/net/nft_nat_table.h"
#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/cvdalloc/interface.h"
#include "cuttlefish/host/commands/cvdalloc/privilege.h"
#include "cuttlefish/host/commands/cvdalloc/sem.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/posix/temp_failure_retry.h"  // IWYU pragma: keep
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

ABSL_FLAG(int, id, 0, "Id");
ABSL_FLAG(int, socket, 0, "Socket");

namespace cuttlefish {
namespace {

void Usage() {
  LOG(ERROR) << "cvdalloc --id=id --socket=fd ";
  LOG(ERROR) << "Should only be invoked from run_cvd.";
}

Result<void> Allocate(int id, absl::string_view ethernet_bridge_name,
                      absl::string_view wireless_bridge_name) {
  LOG(INFO) << "cvdalloc: allocating network resources";

  CF_EXPECT(CreateMobileIface(CvdallocInterfaceName("mtap", id), id,
                              kCvdallocMobileIpPrefix));
  CF_EXPECT(CreateEthernetBridgeIface(wireless_bridge_name,
                                      kCvdallocWirelessIpPrefix));
  CF_EXPECT(CreateEthernetIface(CvdallocInterfaceName("wtap", id),
                                wireless_bridge_name));
  CF_EXPECT(CreateMobileIface(CvdallocInterfaceName("wifiap", id), id,
                              kCvdallocWirelessApIpPrefix));
  CF_EXPECT(CreateEthernetBridgeIface(ethernet_bridge_name,
                                      kCvdallocEthernetIpPrefix));
  CF_EXPECT(CreateEthernetIface(CvdallocInterfaceName("etap", id),
                                ethernet_bridge_name));

  return {};
}

Result<void> Teardown(int id, absl::string_view ethernet_bridge_name,
                      absl::string_view wireless_bridge_name) {
  LOG(INFO) << "cvdalloc: tearing down resources";

  DestroyMobileIface(CvdallocInterfaceName("mtap", id), id,
                     kCvdallocMobileIpPrefix);
  DestroyEthernetIface(CvdallocInterfaceName("wtap", id));
  DestroyMobileIface(CvdallocInterfaceName("wifiap", id), id,
                     kCvdallocWirelessApIpPrefix);
  DestroyEthernetIface(CvdallocInterfaceName("etap", id));
  DestroyEthernetBridgeIface(ethernet_bridge_name, kCvdallocEthernetIpPrefix);
  DestroyEthernetBridgeIface(wireless_bridge_name, kCvdallocWirelessIpPrefix);

  return {};
}

std::string NatTableName(int id) { return absl::StrCat("cvdalloc_", id); }

Result<NftNatTable> CreateNatTable(int id) {
  // Masquerade the shared bridges in every instance's table, since we don't
  // know which one will be the last to use them.
  return NftNatTable::Create(
      NatTableName(id),
      {
          MobileNetworkName(kCvdallocMobileIpPrefix, kMobileNetmask, id),
          MobileNetworkName(kCvdallocWirelessApIpPrefix, kMobileNetmask, id),
          absl::StrCat(kCvdallocWirelessIpPrefix, ".0/24"),
          absl::StrCat(kCvdallocEthernetIpPrefix, ".0/24"),
      });
}

}  // namespace

Result<int> CvdallocMain(int argc, char* argv[]) {
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);

  if (absl::GetFlag(FLAGS_id) == 0 || absl::GetFlag(FLAGS_socket) == 0) {
    Usage();
    /* No need to dump a trace for usage. */
    return 1;
  }

  const int id = absl::GetFlag(FLAGS_id);
  CF_EXPECTF(id >= 1 && id <= static_cast<int>(kMaxIfaceNameId),
             "id must be between 1 and {}, got {}", kMaxIfaceNameId, id);

  SharedFD sock = CF_EXPECT(Fd::Dup(absl::GetFlag(FLAGS_socket)));
  const int r = TEMP_FAILURE_RETRY(close(absl::GetFlag(FLAGS_socket)));
  if (r == -1) {
    return CF_ERRNO("close: " << StrError());
  }

  absl::Cleanup shutdown = [sock]() { sock->Shutdown(SHUT_RDWR); };

  // Elevate before creating the table and taps, so they're cleaned up before
  // privileges are dropped.
  const ScopedPrivileges privileges = CF_EXPECT(ScopedPrivileges::Elevate());
  // Create the table before the taps and delete it after them. If another
  // cvdalloc is using this id, we stop here before touching its taps.
  const NftNatTable nat = CF_EXPECT(CreateNatTable(id));

  absl::Cleanup teardown = [id]() {
    LOG(INFO) << "cvdalloc: teardown started";
    // TODO: b/471069557 - diagnose unused
    Result<void> unused =
        Teardown(id, kCvdallocEthernetBridgeName, kCvdallocWirelessBridgeName);
  };

  CF_EXPECT(
      Allocate(id, kCvdallocEthernetBridgeName, kCvdallocWirelessBridgeName));
  CF_EXPECT(cvdalloc::Post(sock));

  LOG(INFO) << "cvdalloc: waiting to teardown";

  CF_EXPECT(cvdalloc::Wait(sock, cvdalloc::kSemNoTimeout));
  std::move(teardown).Invoke();
  CF_EXPECT(cvdalloc::Post(sock));

  return 0;
}

}  // namespace cuttlefish

int main(int argc, char* argv[]) {
  auto res = cuttlefish::CvdallocMain(argc, argv);
  if (!res.has_value()) {
    LOG(ERROR) << "cvdalloc failed: \n" << res.error();
    abort();
  }

  return *res;
}
