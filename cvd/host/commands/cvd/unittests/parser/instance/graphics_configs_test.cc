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

#include <gtest/gtest.h>
#include "host/commands/cvd/parser/launch_cvd_parser.h"
#include "host/commands/cvd/unittests/parser/test_common.h"

namespace cuttlefish {

TEST(BootFlagsParserTest, ParseTwoInstancesDisplaysFlagEmptyJson) {
  const char* test_string = R""""(
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

  const char* expected_string =
      R""""(--displays_binproto=Cg0KCwjQBRCAChjAAiA8Cg0KCwjQBRCAChjAAiA8)"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = ParseLaunchCvdConfigs(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, expected_string))
      << "extra_bootconfig_args flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesDisplaysFlagEmptyGraphics) {
  const char* test_string = R""""(
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

  const char* expected_string =
      R""""(--displays_binproto=Cg0KCwjQBRCAChjAAiA8Cg0KCwjQBRCAChjAAiA8)"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = ParseLaunchCvdConfigs(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, expected_string))
      << "extra_bootconfig_args flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesDisplaysFlagEmptyDisplays) {
  const char* test_string = R""""(
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

  const char* expected_string =
      R""""(--displays_binproto=Cg0KCwjQBRCAChjAAiA8ChoKCwjQBRCAChjAAiA8CgsI0AUQgAoYwAIgPA==)"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = ParseLaunchCvdConfigs(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, expected_string))
      << "extra_bootconfig_args flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesAutoTabletDisplaysFlag) {
  const char* test_string = R""""(
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

  const char* expected_string =
      R""""(--displays_binproto=ChgKCgi4CBDYBBh4IDwKCgiQAxDYBBh4IDwKDQoLCIAUEIgOGMACIDw=)"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = ParseLaunchCvdConfigs(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, expected_string))
      << "extra_bootconfig_args flag is missing or wrongly formatted";
}

}  // namespace cuttlefish
