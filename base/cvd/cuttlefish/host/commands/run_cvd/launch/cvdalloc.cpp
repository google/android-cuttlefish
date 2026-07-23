//
// Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/run_cvd/launch/cvdalloc.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "fruit/component.h"
#include "fruit/fruit_forward_decls.h"

#include "allocd/alloc_utils.h"
#include "cuttlefish/host/commands/cvdalloc/privilege.h"
#include "cuttlefish/host/commands/cvdalloc/sem.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/host/libs/feature/feature.h"
#include "cuttlefish/host/libs/vm_manager/vm_manager.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/process/subprocess.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

constexpr std::chrono::seconds kCvdAllocateTimeout = std::chrono::seconds(30);
constexpr std::chrono::seconds kCvdTeardownTimeout = std::chrono::seconds(2);

enum class CvdallocStatus { kUnknown = 0, kAvailable, kFailed };

Cvdalloc::Cvdalloc(const CuttlefishConfig::InstanceSpecific& instance)
    : instance_(instance), status_(CvdallocStatus::kUnknown) {}

Result<std::vector<MonitorCommand>> Cvdalloc::Commands() {
  std::string path = CvdallocBinary();
  CF_EXPECT(BinaryIsValid(path));
  auto nice_stop = [this]() { return Stop(); };

  Command cmd(path, KillSubprocessFallback(nice_stop));
  cmd.AddParameter("--id=", instance_.id());
  cmd.AddParameter("--socket=", their_socket_);
  std::vector<MonitorCommand> commands;
  commands.emplace_back(std::move(cmd));
  return commands;
}

std::string Cvdalloc::Name() const { return "Cvdalloc"; }

bool Cvdalloc::Enabled() const {
  if (!IsUsable().has_value()) {
    return false;
  }
  return instance_.use_cvdalloc();
}

std::unordered_set<SetupFeature*> Cvdalloc::Dependencies() const { return {}; }

Result<void> Cvdalloc::WaitForAvailability() {
  std::lock_guard<std::mutex> lock(availability_mutex_);
  CF_EXPECT(status_ != CvdallocStatus::kFailed);
  if (status_ == CvdallocStatus::kUnknown) {
    LOG(INFO) << "cvdalloc (run_cvd): waiting to finish allocation.";
    status_ = CvdallocStatus::kFailed;
    CF_EXPECT(cvdalloc::Wait(socket_, kCvdAllocateTimeout),
              "cvdalloc (run_cvd): Wait failed");
    LOG(INFO) << "cvdalloc (run_cvd): allocation is done.";
    status_ = CvdallocStatus::kAvailable;
  }

  return {};
}

Result<void> Cvdalloc::ResultSetup() {
  std::pair<SharedFD, SharedFD> p =
      CF_EXPECT(SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0));
  socket_ = std::move(p.first);
  their_socket_ = std::move(p.second);

  return {};
}

Result<void> Cvdalloc::BinaryIsValid(std::string_view path) {
  struct stat st;  // NOLINT(misc-include-cleaner): sys/stat.h
  int r = stat(path.data(), &st);
  CF_EXPECT(r == 0, "Could not stat the cvdalloc binary at "
                        << path << ": " << StrError(errno));
  CF_EXPECT(ValidateCvdallocBinary(path));
  return {};
}

Result<void> Cvdalloc::IsUsable() const {
  CF_EXPECT(IptablesPath());
  return {};
}

StopperResult Cvdalloc::Stop() {
  LOG(INFO) << "cvdalloc (run_cvd): stop requested; teardown started";
  if (!cvdalloc::Post(socket_).has_value()) {
    LOG(INFO) << "cvdalloc (run_cvd): stop failed: couldn't Post";
    return StopperResult::kFailure;
  }

  if (!cvdalloc::Wait(socket_, kCvdTeardownTimeout).has_value()) {
    LOG(INFO) << "cvdalloc (run_cvd): stop failed: couldn't Wait";
    return StopperResult::kFailure;
  }

  LOG(INFO) << "cvdalloc (run_cvd): teardown completed";
  return StopperResult::kSuccess;
}

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
CvdallocComponent() {
  return fruit::createComponent()
      .addMultibinding<vm_manager::VmmDependencyCommand, Cvdalloc>()
      .addMultibinding<CommandSource, Cvdalloc>()
      .addMultibinding<SetupFeature, Cvdalloc>();
}

}  // namespace cuttlefish
