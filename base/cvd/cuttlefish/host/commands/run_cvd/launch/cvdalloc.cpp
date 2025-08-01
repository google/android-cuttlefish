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
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <chrono>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <fruit/component.h>
#include <fruit/fruit_forward_decls.h>
#include <fruit/macro.h>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/cvdalloc/sem.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/host/libs/feature/feature.h"
#include "cuttlefish/host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {
namespace {

constexpr std::chrono::seconds kCvdAllocateTimeout = std::chrono::seconds(30);
constexpr std::chrono::seconds kCvdTeardownTimeout = std::chrono::seconds(2);

class Cvdalloc : public vm_manager::VmmDependencyCommand {
 public:
  INJECT(Cvdalloc(const CuttlefishConfig::InstanceSpecific &instance))
      : instance_(instance) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
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

  std::string Name() const override { return "Cvdalloc"; }
  bool Enabled() const override { return instance_.use_cvdalloc(); }
  std::unordered_set<SetupFeature *> Dependencies() const override {
    return {};
  }

  // StatusCheckCommandSource
  Result<void> WaitForAvailability() const override {
    LOG(INFO) << "cvdalloc (run_cvd): waiting to finish allocation.";
    CF_EXPECT(cvdalloc::Wait(socket_, kCvdAllocateTimeout),
              "cvdalloc (run_cvd): Wait failed");
    LOG(INFO) << "cvdalloc (run_cvd): allocation is done.";

    return {};
  }

 private:
  Result<void> ResultSetup() override {
    std::pair<SharedFD, SharedFD> p =
        CF_EXPECT(SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0));
    socket_ = std::move(p.first);
    their_socket_ = std::move(p.second);
    return {};
  }

  Result<void> BinaryIsValid(std::string_view path) {
    struct stat st;
    int r = stat(path.data(), &st);
    CF_EXPECT(r == 0, "Could not stat the cvdalloc binary at "
                          << path << ": " << strerror(errno));

    CF_EXPECTF((st.st_mode & S_ISUID) != 0 && st.st_uid == 0,
        "cvdalloc binary does not have permissions to allocate resources.\n"
        "As root, please\n\n    chown root {}\n    chmod u+s {}\n\n"
        "and start the instance again.",
        path, path);

    return {};
  }

  StopperResult Stop() {
    LOG(INFO) << "cvdalloc (run_cvd): stop requested; teardown started";
    if (!cvdalloc::Post(socket_).ok()) {
      LOG(INFO) << "cvdalloc (run_cvd): stop failed: couldn't Post";
      return StopperResult::kStopFailure;
    }

    if (!cvdalloc::Wait(socket_, kCvdTeardownTimeout).ok()) {
      LOG(INFO) << "cvdalloc (run_cvd): stop failed: couldn't Wait";
      return StopperResult::kStopFailure;
    }

    LOG(INFO) << "cvdalloc (run_cvd): teardown completed";
    return StopperResult::kStopSuccess;
  }

  const CuttlefishConfig::InstanceSpecific &instance_;
  SharedFD socket_, their_socket_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
CvdallocComponent() {
  return fruit::createComponent()
      .addMultibinding<vm_manager::VmmDependencyCommand, Cvdalloc>()
      .addMultibinding<CommandSource, Cvdalloc>()
      .addMultibinding<SetupFeature, Cvdalloc>();
}

}  // namespace cuttlefish
