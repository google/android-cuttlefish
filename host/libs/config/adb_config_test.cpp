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

#include <gtest/gtest.h>
#include <string>

namespace cuttlefish {

TEST(AdbConfigTest, SaveRetrieve) {
  AdbConfig config;
  std::set<AdbMode> modes = {AdbMode::VsockTunnel, AdbMode::VsockHalfTunnel,
                             AdbMode::NativeVsock, AdbMode::Unknown};
  config.set_adb_mode(modes);
  config.set_run_adb_connector(true);

  ASSERT_EQ(config.adb_mode(), modes);
  ASSERT_TRUE(config.run_adb_connector());
}

TEST(AdbConfigTest, SerializeDeserialize) {
  AdbConfig config;
  config.set_adb_mode({AdbMode::VsockTunnel, AdbMode::VsockHalfTunnel,
                       AdbMode::NativeVsock, AdbMode::Unknown});
  config.set_run_adb_connector(true);

  AdbConfig config2;
  ASSERT_TRUE(config2.Deserialize(config.Serialize()));
  ASSERT_EQ(config.adb_mode(), config2.adb_mode());
  ASSERT_EQ(config.run_adb_connector(), config2.run_adb_connector());
}

}  // namespace cuttlefish
