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

#pragma once

#include <mutex>
#include <string_view>

#include <fruit/fruit.h>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/host/libs/feature/feature.h"
#include "cuttlefish/host/libs/vm_manager/vm_manager.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

enum class CvdallocStatus;

class Cvdalloc : public vm_manager::VmmDependencyCommand {
 public:
  INJECT(Cvdalloc(const CuttlefishConfig::InstanceSpecific &instance));

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override;
  std::string Name() const override;
  bool Enabled() const override;
  std::unordered_set<SetupFeature *> Dependencies() const override;

  // StatusCheckCommandSource
  Result<void> WaitForAvailability() override;

 private:
  Result<void> ResultSetup() override;
  Result<void> BinaryIsValid(std::string_view path);
  StopperResult Stop();

  const CuttlefishConfig::InstanceSpecific &instance_;
  SharedFD socket_, their_socket_;
  std::mutex availability_mutex_;
  CvdallocStatus status_;
};

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
CvdallocComponent();

}  // namespace cuttlefish
