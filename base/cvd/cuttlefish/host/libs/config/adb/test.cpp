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

#include <fruit/fruit.h>
#include <gtest/gtest.h>
#include <host/libs/config/adb/adb.h>

#include <string>

#include "host/libs/config/feature.h"

namespace cuttlefish {

struct TestData {
  INJECT(TestData(AdbConfig& config, AdbConfigFragment& fragment))
      : config(config), fragment(fragment) {}

  AdbConfig& config;
  AdbConfigFragment& fragment;
};

fruit::Component<TestData> TestComponent() {
  return fruit::createComponent()
      .install(AdbConfigComponent)
      .install(AdbConfigFlagComponent)
      .install(AdbConfigFragmentComponent)
      .install(ConfigFlagPlaceholder);
}

TEST(AdbConfigTest, SetFromFlags) {
  fruit::Injector<TestData> injector(TestComponent);
  TestData& data = injector.get<TestData&>();
  std::vector<std::string> args = {
      "--adb_mode=vsock_tunnel,vsock_half_tunnel,native_vsock,unknown",
      "--run_adb_connector=false",
  };
  auto flags = injector.getMultibindings<FlagFeature>();
  auto processed = FlagFeature::ProcessFlags(flags, args);
  ASSERT_TRUE(processed.ok()) << processed.error().Trace();
  ASSERT_TRUE(args.empty());

  std::set<AdbMode> modes = {AdbMode::VsockTunnel, AdbMode::VsockHalfTunnel,
                             AdbMode::NativeVsock, AdbMode::Unknown};
  ASSERT_EQ(data.config.Modes(), modes);
  ASSERT_FALSE(data.config.RunConnector());
}

TEST(AdbConfigTest, SerializeDeserialize) {
  fruit::Injector<TestData> injector1(TestComponent);
  TestData& data1 = injector1.get<TestData&>();
  ASSERT_TRUE(
      data1.config.SetModes({AdbMode::VsockTunnel, AdbMode::VsockHalfTunnel,
                             AdbMode::NativeVsock, AdbMode::Unknown}));
  ASSERT_TRUE(data1.config.SetRunConnector(false));

  fruit::Injector<TestData> injector2(TestComponent);
  TestData& data2 = injector2.get<TestData&>();
  ASSERT_TRUE(data2.fragment.Deserialize(data1.fragment.Serialize()));
  ASSERT_EQ(data1.config.Modes(), data2.config.Modes());
  ASSERT_EQ(data1.config.RunConnector(), data2.config.RunConnector());
}

}  // namespace cuttlefish
