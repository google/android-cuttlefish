/*
 * Copyright (C) 2022 The Android Open Source Project
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
#include "host/libs/config/fastboot/fastboot.h"

#include <android-base/logging.h>
#include <json/json.h>

#include "host/libs/config/config_fragment.h"

namespace cuttlefish {
namespace {

class FastbootConfigFragmentImpl : public FastbootConfigFragment {
 public:
  INJECT(FastbootConfigFragmentImpl(FastbootConfig& config)) : config_(config) {}

  std::string Name() const override { return "FastbootConfigFragmentImpl"; }

  Json::Value Serialize() const override {
    Json::Value json;
    json[kProxyFastboot] = config_.ProxyFastboot();
    return json;
  }

  bool Deserialize(const Json::Value& json) override {
    if (!json.isMember(kProxyFastboot) ||
        json[kProxyFastboot].type() != Json::booleanValue) {
      LOG(ERROR) << "Invalid value for " << kProxyFastboot;
      return false;
    }
    if (!config_.SetProxyFastboot(json[kProxyFastboot].asBool())) {
      LOG(ERROR) << "Failed to set whether to run the fastboot proxy";
    }
    return true;
  }

 private:
  static constexpr char kProxyFastboot[] = "proxy_fastboot";
  FastbootConfig& config_;
};

}  // namespace

fruit::Component<fruit::Required<FastbootConfig>, FastbootConfigFragment>
FastbootConfigFragmentComponent() {
  return fruit::createComponent()
      .bind<FastbootConfigFragment, FastbootConfigFragmentImpl>()
      .addMultibinding<ConfigFragment, FastbootConfigFragment>();
}

}  // namespace cuttlefish
