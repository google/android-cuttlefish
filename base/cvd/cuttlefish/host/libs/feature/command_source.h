//
// Copyright (C) 2019 The Android Open Source Project
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

#include <ostream>
#include <string_view>
#include <utility>
#include <vector>

#include "fruit/fruit.h"

#include "cuttlefish/host/libs/feature/feature.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

enum class ProcessCategory {
  kNonCriticalSupport,
  kCriticalSupport,
  kVmm,
};

inline constexpr std::string_view format_as(ProcessCategory category) {
  switch (category) {
    case ProcessCategory::kVmm:
      return "vmm";
    case ProcessCategory::kCriticalSupport:
      return "critical support";
    case ProcessCategory::kNonCriticalSupport:
      return "non-critical support";
  }
}

inline std::ostream& operator<<(std::ostream& out, ProcessCategory category) {
  return out << format_as(category);
}

struct MonitorCommand {
  Command command;
  bool is_critical;
  ProcessCategory category;

  MonitorCommand(Command command, bool is_critical = true)
      : command(std::move(command)),
        is_critical(is_critical),
        category(is_critical ? ProcessCategory::kCriticalSupport
                             : ProcessCategory::kNonCriticalSupport) {}

  MonitorCommand(Command command, ProcessCategory category)
      : command(std::move(command)),
        is_critical(category != ProcessCategory::kNonCriticalSupport),
        category(category) {}
};

class CommandSource : public virtual SetupFeature {
 public:
  virtual ~CommandSource() = default;
  virtual Result<std::vector<MonitorCommand>> Commands() = 0;
};

class StatusCheckCommandSource : public virtual CommandSource {
 public:
  virtual Result<void> WaitForAvailability() = 0;
};

}  // namespace cuttlefish
