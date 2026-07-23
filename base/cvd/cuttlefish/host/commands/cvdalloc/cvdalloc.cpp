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
#include <unistd.h>

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"

#include "allocd/alloc_utils.h"
#include "allocd/net/nftables_nft.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/cvdalloc/interface.h"
#include "cuttlefish/host/commands/cvdalloc/privilege.h"
#include "cuttlefish/host/commands/cvdalloc/sem.h"
#include "cuttlefish/posix/strerror.h"

ABSL_FLAG(int, id, 0, "Id");
ABSL_FLAG(int, socket, 0, "Socket");
ABSL_FLAG(bool, setup, false,
          "Set up nftables tables, chains, and static bridge NAT rules");
ABSL_FLAG(bool, teardown, false, "Tear down nftables tables");

namespace cuttlefish {
namespace {

// The mutually exclusive modes in which cvdalloc can run.
enum class Mode {
  kSetup,     // Install the static nftables environment.
  kTeardown,  // Remove the static nftables environment.
  kInstance,  // Allocate resources for a single instance.
};

void Usage() {
  LOG(ERROR)
      << "Usage: cvdalloc [--setup | --teardown | --id=id --socket=fd]";
  LOG(ERROR) << "Should only be invoked from cuttlefish-host-resources or "
                "run_cvd.";
}

std::optional<Mode> DetermineMode() {
  const bool is_setup = absl::GetFlag(FLAGS_setup);
  const bool is_teardown = absl::GetFlag(FLAGS_teardown);

  if (is_setup && is_teardown) {
    return std::nullopt;
  }
  if (is_setup) {
    return Mode::kSetup;
  }
  if (is_teardown) {
    return Mode::kTeardown;
  }
  if (absl::GetFlag(FLAGS_id) == 0 || absl::GetFlag(FLAGS_socket) == 0) {
    return std::nullopt;
  }
  return Mode::kInstance;
}

Result<std::vector<NftRule>> Allocate(Nftables& nft, int id,
                                      std::string_view ethernet_bridge_name,
                                      std::string_view wireless_bridge_name) {
  LOG(INFO) << "cvdalloc: allocating network resources";

  std::vector<NftRule> rules;

  rules.push_back(CF_EXPECT(CreateMobileIface(
      nft, CvdallocInterfaceName("mtap", id), id, kCvdallocMobileIpPrefix)));

  CF_EXPECT(CreateEthernetBridgeIface(wireless_bridge_name,
                                      kCvdallocWirelessIpPrefix));

  CF_EXPECT(CreateEthernetIface(CvdallocInterfaceName("wtap", id),
                                wireless_bridge_name));

  rules.push_back(CF_EXPECT(CreateMobileIface(
      nft, CvdallocInterfaceName("wifiap", id), id,
      kCvdallocWirelessApIpPrefix)));

  CF_EXPECT(CreateEthernetBridgeIface(ethernet_bridge_name,
                                      kCvdallocEthernetIpPrefix));

  CF_EXPECT(CreateEthernetIface(CvdallocInterfaceName("etap", id),
                                ethernet_bridge_name));

  return rules;
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
  DestroyEthernetIface(CvdallocInterfaceName("etap", id));
  DestroyBridge(ethernet_bridge_name);
  DestroyBridge(wireless_bridge_name);

  return {};
}

// Adopts the socket file descriptor passed on the command line: duplicates it
// into a SharedFD and closes the original numeric descriptor.
Result<SharedFD> AdoptSocket(int socket_fd) {
  SharedFD sock = SharedFD::Dup(socket_fd);
  CF_EXPECT(sock->IsOpen(), "cvdalloc: socket is closed: " << sock->StrError());
  CF_EXPECTF(TEMP_FAILURE_RETRY(close(socket_fd)) != -1, "close: {}",
             StrError(errno));
  return sock;
}

// Installs the static nftables environment (tables, chains, and the static
// bridge NAT rules) so that dynamic per-instance rules can be added later.
Result<void> CvdallocSetup(Nftables& nft) {
  LOG(INFO) << "cvdalloc: running setup";
  CF_EXPECT(SetupFirewall(nft));
  return {};
}

// Removes the static nftables environment, which atomically cleans up every
// chain and rule the tables contain.
Result<void> CvdallocTeardown(Nftables& nft) {
  LOG(INFO) << "cvdalloc: running teardown";
  CF_EXPECT(TeardownFirewall(nft));
  return {};
}

// Allocates per-instance network resources, signals readiness to run_cvd,
// blocks until the shutdown signal, and then releases the resources.
Result<void> CvdallocMain(Nftables& nft, int id, int socket_fd) {
  SharedFD sock = CF_EXPECT(AdoptSocket(socket_fd));
  absl::Cleanup shutdown = [sock]() { sock->Shutdown(SHUT_RDWR); };

  // Release all resources if we bail out before the normal teardown below.
  absl::Cleanup teardown = [id]() {
    LOG(INFO) << "cvdalloc: teardown started";
    Result<void> unused =
        Teardown(id, kCvdallocEthernetBridgeName, kCvdallocWirelessBridgeName);
  };

  // Allocate resources, then signal run_cvd that the instance is ready.
  std::vector<NftRule> rules = CF_EXPECT(Allocate(
      nft, id, kCvdallocEthernetBridgeName, kCvdallocWirelessBridgeName));
  CF_EXPECT(cvdalloc::Post(sock));

  // Block until run_cvd signals that the instance is tearing down.
  LOG(INFO) << "cvdalloc: waiting to teardown";
  CF_EXPECT(cvdalloc::Wait(sock, cvdalloc::kSemNoTimeout));

  // Release resources and acknowledge completion.
  std::move(teardown).Invoke();
  rules.clear();
  CF_EXPECT(cvdalloc::Post(sock));

  return {};
}

}  // namespace

Result<int> RunCvdalloc(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);

  std::optional<Mode> mode = DetermineMode();
  if (!mode.has_value()) {
    Usage();
    return 1;
  }

  ScopedPrivileges privileges = CF_EXPECT(ScopedPrivileges::Elevate());

  NftablesNft nft;
  switch (*mode) {
    case Mode::kSetup:
      CF_EXPECT(CvdallocSetup(nft));
      return 0;
    case Mode::kTeardown:
      CF_EXPECT(CvdallocTeardown(nft));
      return 0;
    case Mode::kInstance:
      CF_EXPECT(CvdallocMain(nft, absl::GetFlag(FLAGS_id),
                             absl::GetFlag(FLAGS_socket)));
      return 0;
  }

  return CF_ERR("cvdalloc: unhandled run mode");
}

}  // namespace cuttlefish

int main(int argc, char* argv[]) {
  auto res = cuttlefish::RunCvdalloc(argc, argv);
  if (!res.ok()) {
    LOG(ERROR) << "cvdalloc failed: \n" << res.error();
    abort();
  }

  return *res;
}
