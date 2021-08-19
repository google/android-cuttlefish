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

#include <host/libs/config/adb_config.h>

#include <fruit/fruit.h>
#include <gtest/gtest.h>
#include <string>

#include "host/libs/config/feature.h"

namespace cuttlefish {

class TestInjector {
 public:
  TestInjector() : injector_(TestComponent) {}

  AdbConfig& Config() { return injector_.get<AdbConfig&>(); }

  bool ParseArguments(std::vector<std::string>& args) {
    auto flags = injector_.getMultibindings<FlagFeature>();
    return FlagFeature::ProcessFlags(flags, args);
  }

 private:
  static fruit::Component<AdbConfig> TestComponent() {
    return fruit::createComponent()
        .install(ConfigFlagPlaceholder)
        .install(AdbConfigComponent);
  }

  fruit::Injector<AdbConfig> injector_;
};

TEST(AdbConfigTest, SetFromFlags) {
  TestInjector env;
  std::vector<std::string> args = {
      "--adb_mode=vsock_tunnel,vsock_half_tunnel,native_vsock,unknown",
      "--run_adb_connector=false",
  };
  ASSERT_TRUE(env.ParseArguments(args));
  ASSERT_TRUE(args.empty());

  std::set<AdbMode> modes = {AdbMode::VsockTunnel, AdbMode::VsockHalfTunnel,
                             AdbMode::NativeVsock, AdbMode::Unknown};
  ASSERT_EQ(env.Config().adb_mode(), modes);
  ASSERT_FALSE(env.Config().run_adb_connector());
}

TEST(AdbConfigTest, SerializeDeserialize) {
  TestInjector env;
  std::vector<std::string> args = {
      "--adb_mode=vsock_tunnel,vsock_half_tunnel,native_vsock,unknown",
      "--run_adb_connector=false",
  };
  ASSERT_TRUE(env.ParseArguments(args));
  ASSERT_TRUE(args.empty());

  TestInjector env2;
  ASSERT_TRUE(env2.Config().Deserialize(env.Config().Serialize()));
  ASSERT_EQ(env.Config().adb_mode(), env2.Config().adb_mode());
  ASSERT_EQ(env.Config().run_adb_connector(),
            env2.Config().run_adb_connector());
}

}  // namespace cuttlefish
