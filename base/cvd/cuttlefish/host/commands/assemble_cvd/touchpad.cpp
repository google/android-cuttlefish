//
// Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/assemble_cvd/touchpad.h"

#include "common/libs/utils/flag_parser.h"
#include "host/libs/config/touchpad.h"

namespace cuttlefish {
namespace {

class TouchpadsConfigsImpl : public TouchpadsConfigs {
 public:
  INJECT(TouchpadsConfigsImpl()) {}

  const std::vector<CuttlefishConfig::TouchpadConfig>& GetConfigs()
      const override {
    return touchpad_configs_;
  }

  void SetConfigs(
      const std::vector<CuttlefishConfig::TouchpadConfig>& configs) {
    touchpad_configs_ = configs;
  }

  std::string Name() const override { return "TouchpadsConfigsImpl"; }

 private:
  std::vector<CuttlefishConfig::TouchpadConfig> touchpad_configs_;
};

}  // namespace

fruit::Component<TouchpadsConfigs> TouchpadsConfigsComponent() {
  return fruit::createComponent()
      .bind<TouchpadsConfigs, TouchpadsConfigsImpl>()
      .addMultibinding<TouchpadsConfigs, TouchpadsConfigs>();
}

namespace {

class TouchpadsConfigsFlagImpl : public TouchpadsConfigsFlag {
 public:
  INJECT(TouchpadsConfigsFlagImpl(TouchpadsConfigs& configs,
                                  ConfigFlag& config_flag))
      : touchpad_configs_(configs), config_flag_dependency_(config_flag) {}

  std::string Name() const override { return "TouchpadsConfigsFlagImpl"; }

  std::unordered_set<FlagFeature*> Dependencies() const override {
    return {static_cast<FlagFeature*>(&config_flag_dependency_)};
  }

  Result<void> Process(std::vector<std::string>& args) override {
    touchpad_configs_.SetConfigs(CF_EXPECT(ParseTouchpadConfigsFromArgs(args)));
    return {};
  }

  bool WriteGflagsCompatHelpXml(std::ostream& out) const override {
    Flag touchpad_flag = GflagsCompatFlag(kTouchpadFlag).Help(kTouchpadHelp);
    return WriteGflagsCompatXml({touchpad_flag}, out);
  }

 private:
  TouchpadsConfigs& touchpad_configs_;
  ConfigFlag& config_flag_dependency_;
};

}  // namespace

fruit::Component<fruit::Required<TouchpadsConfigs, ConfigFlag>,
                 TouchpadsConfigsFlag>
TouchpadsConfigsFlagComponent() {
  return fruit::createComponent()
      .bind<TouchpadsConfigsFlag, TouchpadsConfigsFlagImpl>()
      .addMultibinding<FlagFeature, TouchpadsConfigsFlag>();
}

}  // namespace cuttlefish
