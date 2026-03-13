/*
 * Copyright (C) 2015-2022 The Android Open Source Project
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

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include <google/protobuf/message.h>
#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>

#include "cuttlefish/common/libs/utils/base64.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/host/commands/assemble_cvd/proto/launch_cvd.pb.h"
#include "cuttlefish/host/commands/cvd/cli/parser/test_common.h"
#include "cuttlefish/result/result_matchers.h"

using google::protobuf::Message;
using google::protobuf::util::MessageDifferencer;

namespace cuttlefish {

// TODO: schuffelen - make this into a matcher
static void AssertProtoEquals(const Message& expected, const Message& actual) {
  MessageDifferencer diff;
  std::string diff_str;
  diff.ReportDifferencesToString(&diff_str);
  EXPECT_TRUE(diff.Compare(expected, actual)) << diff_str;
}

InstanceDisplays DefaultDisplays() {
  InstanceDisplays displays;
  auto& display = *displays.add_displays();
  display.set_width(720);
  display.set_height(1280);
  display.set_dpi(320);
  display.set_refresh_rate_hertz(60);
  return displays;
}

Result<std::optional<InstancesDisplays>> DisplaysFlag(std::vector<std::string> args) {
  std::optional<std::string> flag_str_opt;
  auto flag = GflagsCompatFlag("displays_binproto")
                  .Setter([&flag_str_opt](const FlagMatch& m) -> Result<void> {
                    flag_str_opt = m.value;
                    return {};
                  });
  CF_EXPECT(ConsumeFlags({flag}, args));
  if (!flag_str_opt.has_value()) {
    return {};
  }
  auto flag_str = CF_EXPECT(std::move(flag_str_opt));

  std::vector<uint8_t> decoded;
  CF_EXPECT(DecodeBase64(flag_str, &decoded));

  InstancesDisplays ret;
  CF_EXPECT(ret.ParseFromArray(decoded.data(), decoded.size()));
  return ret;
}

TEST(BootFlagsParserTest, ParseTwoInstancesDisplaysFlagEmptyJson) {
  static constexpr char kTestString[] = R""""(
{
    "instances" :
    [
        {
        },
        {
        }
    ]
}
)"""";

  auto json_configs = ParseJson(kTestString);
  EXPECT_THAT(json_configs, IsOk());

  auto serialized_data = LaunchCvdParserTester(*json_configs);
  EXPECT_THAT(serialized_data, IsOk());

  auto display = DisplaysFlag(*serialized_data);
  EXPECT_THAT(display, IsOk());
}

TEST(BootFlagsParserTest, ParseTwoInstancesDisplaysFlagEmptyGraphics) {
  static constexpr char kTestString[] = R""""(
{
    "instances" :
    [
        {
            "graphics": {
            }
        },
        {
            "graphics": {
            }
        }
    ]
}
  )"""";

  auto json_configs = ParseJson(kTestString);
  EXPECT_THAT(json_configs, IsOk());

  auto serialized_data = LaunchCvdParserTester(*json_configs);
  EXPECT_THAT(serialized_data, IsOk());

  auto display = DisplaysFlag(*serialized_data);
  EXPECT_THAT(display, IsOk());
}

TEST(BootFlagsParserTest, ParseTwoInstancesDisplaysFlagEmptyDisplays) {
  static constexpr char kTestString[] = R""""(
{
    "instances" :
    [
        {
            "graphics":{
                "displays":[
                    {
                    }
                ]
                }
        },
        {
            "graphics":{
                "displays":[
                    {
                    },
                    {
                    }
                ]
                }
        }
    ]
}
)"""";

  auto json_configs = ParseJson(kTestString);
  EXPECT_THAT(json_configs, IsOk());

  auto serialized_data = LaunchCvdParserTester(*json_configs);
  EXPECT_THAT(serialized_data, IsOk());

  auto display = DisplaysFlag(*serialized_data);
  EXPECT_THAT(display, IsOk());

  InstancesDisplays expected;
  expected.add_instances()->CopyFrom(DefaultDisplays());
  auto& ins2 = *expected.add_instances();
  ins2.CopyFrom(DefaultDisplays());
  ins2.add_displays()->CopyFrom(ins2.displays()[0]);

  AssertProtoEquals(expected, (*display).value());
}

TEST(BootFlagsParserTest, ParseTwoInstancesAutoTabletDisplaysFlag) {
  static constexpr char kTestString[] = R""""(
{
    "instances" :
    [
        {
            "graphics":{
                "displays":[
                    {
                        "width": 1080,
                        "height": 600,
                        "dpi": 120,
                        "refresh_rate_hertz": 60
                    },
                    {
                        "width": 400,
                        "height": 600,
                        "dpi": 120,
                        "refresh_rate_hertz": 60
                    }
                ]
                }
        },
        {
            "graphics":{
                "displays":[
                    {
                        "width": 2560,
                        "height": 1800,
                        "dpi": 320,
                        "refresh_rate_hertz": 60
                    }
                ]
                }
        }
    ]
}
  )"""";

  auto json_configs = ParseJson(kTestString);
  EXPECT_THAT(json_configs, IsOk());

  auto serialized_data = LaunchCvdParserTester(*json_configs);
  EXPECT_THAT(serialized_data, IsOk());

  auto display = DisplaysFlag(*serialized_data);
  EXPECT_THAT(display, IsOk());

  InstancesDisplays expected;

  auto& instance1 = *expected.add_instances();
  auto& ins1_display1 = *instance1.add_displays();
  ins1_display1.set_width(1080);
  ins1_display1.set_height(600);
  ins1_display1.set_dpi(120);
  ins1_display1.set_refresh_rate_hertz(60);
  auto& ins1_display2 = *instance1.add_displays();
  ins1_display2.set_width(400);
  ins1_display2.set_height(600);
  ins1_display2.set_dpi(120);
  ins1_display2.set_refresh_rate_hertz(60);

  auto& instance2 = *expected.add_instances();
  auto& ins2_display1 = *instance2.add_displays();
  ins2_display1.set_width(2560);
  ins2_display1.set_height(1800);
  ins2_display1.set_dpi(320);
  ins2_display1.set_refresh_rate_hertz(60);

  AssertProtoEquals(expected, (*display).value());
}

}  // namespace cuttlefish
