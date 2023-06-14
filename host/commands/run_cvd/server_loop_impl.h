/*
 * Copyright (C) 2017 The Android Open Source Project
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

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fruit/fruit.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/process_monitor.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/commands/run_cvd/server_loop.h"
#include "host/libs/command_util/util.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/feature.h"
#include "host/libs/config/inject.h"

namespace cuttlefish {
namespace run_cvd_impl {

class ServerLoopImpl : public ServerLoop,
                       public SetupFeature,
                       public LateInjected {
 public:
  INJECT(ServerLoopImpl(const CuttlefishConfig& config,
                        const CuttlefishConfig::InstanceSpecific& instance));

  Result<void> LateInject(fruit::Injector<>& injector) override;

  // ServerLoop
  Result<void> Run() override;

  // SetupFeature
  std::string Name() const override { return "ServerLoop"; }

 private:
  bool Enabled() const override { return true; }
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override;
  Result<void> HandleExtended(const LauncherActionInfo& action_info,
                              const SharedFD& client);
  Result<void> HandleSuspend(const std::string& serialized_data,
                             const SharedFD& client);
  Result<void> HandleResume(const std::string& serialized_data,
                            const SharedFD& client);

  void HandleActionWithNoData(const LauncherAction action,
                              const SharedFD& client,
                              ProcessMonitor& process_monitor);

  void DeleteFifos();
  bool PowerwashFiles();
  void RestartRunCvd(int notification_fd);
  static bool CreateQcowOverlay(const std::string& crosvm_path,
                                const std::string& backing_file,
                                const std::string& output_overlay_path);
  Result<void> SuspendGuest();
  Result<void> ResumeGuest();

  static std::unordered_map<std::string, std::string>
  InitializeVmToControlSockPath(const CuttlefishConfig::InstanceSpecific&);

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  std::vector<CommandSource*> command_sources_;
  SharedFD server_;
  // mapping from the name of vm_manager to control_sock path
  std::unordered_map<std::string, std::string> vm_name_to_control_sock_;
};

}  // namespace run_cvd_impl
}  // namespace cuttlefish
