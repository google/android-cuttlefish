/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unistd.h>
#include <csignal>

#include "absl/log/log.h"
#include "absl/time/time.h"

#include "cuttlefish/host/libs/config/logging.h"
#include "cuttlefish/host/libs/tracing/tracing_server.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<void> WaitForSigterm() {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGTERM);
  CF_EXPECT_EQ(pthread_sigmask(SIG_BLOCK, &set, nullptr), 0,
               "Failed to pthread_sigmask().");

  int received_signal;
  CF_EXPECT_EQ(sigwait(&set, &received_signal), 0,
               "Failed to wait for signal.");

  CF_EXPECT_EQ(received_signal, SIGTERM, "Received unexpected signal.");
  return {};
}

Result<void> RunTracingServer() {
  auto server = CF_EXPECT(TracingServer::StartBlocking(absl::Seconds(10)));
  CF_EXPECT_NE(server, nullptr);
  CF_EXPECT(WaitForSigterm());
  return {};
}

int TracingForwarderMain(int argc, char** argv) {
  auto result = RunTracingServer();
  if (!result.ok()) {
    LOG(ERROR) << "Failed to run tracing server: " << result.error();
    return -1;
  }
  return 0;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::TracingForwarderMain(argc, argv);
}
