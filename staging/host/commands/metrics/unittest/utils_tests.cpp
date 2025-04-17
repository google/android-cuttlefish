/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <string>

#include <gtest/gtest.h>

#include "host/commands/metrics/utils.h"

namespace cuttlefish {
TEST(MacAddressTest, ValidMacAddress) {
  std::string mac = metrics::GetMacAddress();
  ASSERT_FALSE(mac.empty());
  EXPECT_EQ(mac.size(), 17);  // Ensure MAC address has correct length
  // Add other assertions as needed
}

TEST(MacAddressTest, MacAddressFormat) {
  std::string mac = metrics::GetMacAddress();
  // Ensure MAC address has the correct format (e.g., XX:XX:XX:XX:XX:XX)
  EXPECT_EQ(mac[2], ':');
  EXPECT_EQ(mac[5], ':');
  EXPECT_EQ(mac[8], ':');
  EXPECT_EQ(mac[11], ':');
  EXPECT_EQ(mac[14], ':');
}

}  // namespace cuttlefish
