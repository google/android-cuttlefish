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
#include "host/libs/config/adb/adb.h"

#include <fruit/fruit.h>
#include <set>

namespace cuttlefish {
namespace {

class AdbConfigImpl : public AdbConfig {
 public:
  INJECT(AdbConfigImpl()) {}

  const std::set<AdbMode>& Modes() const override { return modes_; }
  bool SetModes(const std::set<AdbMode>& modes) override {
    modes_ = modes;
    return true;
  }
  bool SetModes(std::set<AdbMode>&& modes) override {
    modes_ = std::move(modes);
    return true;
  }

  bool RunConnector() const override { return run_connector_; }
  bool SetRunConnector(bool run) override {
    run_connector_ = run;
    return true;
  }

 private:
  std::set<AdbMode> modes_;
  bool run_connector_;
};

}  // namespace

fruit::Component<AdbConfig> AdbConfigComponent() {
  return fruit::createComponent().bind<AdbConfig, AdbConfigImpl>();
}

}  // namespace cuttlefish
