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

#include "host/libs/config/config_fragment.h"

namespace cuttlefish {

enum class AdbMode {
  VsockTunnel,
  VsockHalfTunnel,
  NativeVsock,
  Unknown,
};

class AdbConfig : public ConfigFragment {
 public:
  INJECT(AdbConfig());

  void set_adb_mode(const std::set<std::string>& modes);
  std::set<AdbMode> adb_mode() const;

  void set_run_adb_connector(bool run_adb_connector);
  bool run_adb_connector() const;

  // ConfigFragment
  std::string Name() const override;
  Json::Value Serialize() const override;
  bool Deserialize(const Json::Value&) override;

 private:
  std::set<AdbMode> adb_mode_;
  bool run_adb_connector_;
};

fruit::Component<AdbConfig> AdbConfigComponent();

}  // namespace cuttlefish
