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

#include <gtest/gtest.h>

#include "common/libs/utils/network.h"

namespace cuttlefish {
namespace {

TEST(CommonUtilNetwork, MacAddressToString) {
  ASSERT_EQ(
      MacAddressToString((uint8_t[6]){0xab, 0xcd, 0xef, 0x12, 0x34, 0x56}),
      "ab:cd:ef:12:34:56");
  ASSERT_EQ(
      MacAddressToString((uint8_t[6]){0x01, 0x02, 0x03, 0x04, 0x05, 0x06}),
      "01:02:03:04:05:06");
}

TEST(CommonUtilNetwork, Ipv6ToString) {
  uint8_t address[] = {0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0xff, 0x00, 0x00, 0x42, 0x83, 0x29};
  ASSERT_EQ(Ipv6ToString(address), "2001:db8::ff00:42:8329");
}

}  // namespace
}  // namespace cuttlefish
