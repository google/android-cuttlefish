/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <android-base/logging.h>
#include <fruit/fruit.h>
#include <json/json.h>

#include "host/libs/config/config_fragment.h"

namespace cuttlefish {
namespace {

class AdbConfigFragmentImpl : public AdbConfigFragment {
 public:
  INJECT(AdbConfigFragmentImpl(AdbConfig& config)) : config_(config) {}

  std::string Name() const override { return "AdbConfigFragmentImpl"; }

  Json::Value Serialize() const override {
    Json::Value json;
    json[kMode] = Json::Value(Json::arrayValue);
    for (const auto& mode : config_.Modes()) {
      json[kMode].append(AdbModeToString(mode));
    }
    json[kConnectorEnabled] = config_.RunConnector();
    return json;
  }
  bool Deserialize(const Json::Value& json) override {
    if (!json.isMember(kMode) || json[kMode].type() != Json::arrayValue) {
      LOG(ERROR) << "Invalid value for " << kMode;
      return false;
    }
    std::set<AdbMode> modes;
    for (auto& mode : json[kMode]) {
      if (mode.type() != Json::stringValue) {
        LOG(ERROR) << "Invalid mode type" << mode;
        return false;
      }
      modes.insert(StringToAdbMode(mode.asString()));
    }
    if (!config_.SetModes(std::move(modes))) {
      LOG(ERROR) << "Failed to set adb modes";
      return false;
    }

    if (!json.isMember(kConnectorEnabled) ||
        json[kConnectorEnabled].type() != Json::booleanValue) {
      LOG(ERROR) << "Invalid value for " << kConnectorEnabled;
      return false;
    }
    if (!config_.SetRunConnector(json[kConnectorEnabled].asBool())) {
      LOG(ERROR) << "Failed to set whether to run the adb connector";
    }
    return true;
  }

 private:
  static constexpr char kMode[] = "mode";
  static constexpr char kConnectorEnabled[] = "connector_enabled";
  AdbConfig& config_;
};

}  // namespace

fruit::Component<fruit::Required<AdbConfig>, AdbConfigFragment>
AdbConfigFragmentComponent() {
  return fruit::createComponent()
      .bind<AdbConfigFragment, AdbConfigFragmentImpl>()
      .addMultibinding<ConfigFragment, AdbConfigFragment>();
}

}  // namespace cuttlefish
