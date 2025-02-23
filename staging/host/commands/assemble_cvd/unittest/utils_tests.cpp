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

#include "common/libs/utils/base64.h"
#include "launch_cvd.pb.h"

namespace cuttlefish {

TEST(DisplayConfigTest, ParseProto) {
  std::string flag_value = "ChoKCgi4CBDYBBh4IDwKDAi4CBDYBBh4IDwqAA==";
  // This is an encoded Display Config with zeros for integer values at end of
  // buffer. (for overlays proto). This is implemented here to catch a corner
  // case with truncated Base64 encodings resulting in error code when
  // serializing.

  std::vector<uint8_t> output;
  DecodeBase64(flag_value, &output);
  std::string serialized = std::string(output.begin(), output.end());

  InstancesDisplays proto_result;
  bool result = proto_result.ParseFromString(serialized);

  EXPECT_EQ(proto_result.instances_size(), 1);

  ASSERT_TRUE(result);
}

}  // namespace cuttlefish
