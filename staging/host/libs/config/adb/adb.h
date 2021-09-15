/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <fruit/fruit.h>
#include <set>

#include "host/libs/config/command_source.h"
#include "host/libs/config/config_flag.h"
#include "host/libs/config/config_fragment.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/feature.h"
#include "host/libs/config/kernel_log_pipe_provider.h"

namespace cuttlefish {

enum class AdbMode {
  VsockTunnel,
  VsockHalfTunnel,
  NativeVsock,
  Unknown,
};

AdbMode StringToAdbMode(const std::string& mode);
std::string AdbModeToString(AdbMode mode);

class AdbConfig {
 public:
  virtual ~AdbConfig() = default;
  virtual const std::set<AdbMode>& Modes() const = 0;
  virtual bool SetModes(const std::set<AdbMode>&) = 0;
  virtual bool SetModes(std::set<AdbMode>&&) = 0;

  virtual bool RunConnector() const = 0;
  virtual bool SetRunConnector(bool) = 0;
};

class AdbConfigFragment : public ConfigFragment {};
class AdbConfigFlag : public FlagFeature {};

fruit::Component<AdbConfig> AdbConfigComponent();
fruit::Component<fruit::Required<AdbConfig, ConfigFlag>, AdbConfigFlag>
AdbConfigFlagComponent();
fruit::Component<fruit::Required<AdbConfig>, AdbConfigFragment>
AdbConfigFragmentComponent();
fruit::Component<fruit::Required<KernelLogPipeProvider, const AdbConfig,
                                 const CuttlefishConfig::InstanceSpecific>>
LaunchAdbComponent();

}  // namespace cuttlefish
