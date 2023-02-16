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
TEST(MetricsFlagsParserTest, ParseOneInstanceMetricsReportInvalidValue) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "metrics": {
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = LaunchCvdParserTester(json_configs);
  EXPECT_FALSE(serialized_data.ok()) << serialized_data.error().Trace();
}

TEST(MetricsFlagsParserTest, ParseOneInstancesMetricsReportFlagEmptyJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = LaunchCvdParserTester(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(
      FindConfig(*serialized_data, R"(--report_anonymous_usage_stats=n)"))
      << "report_anonymous_usage_stats flag is missing or wrongly formatted";
}

TEST(MetricsFlagsParserTest, ParseTwoInstancesMetricsReportFlagEmptyJson) {
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

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = LaunchCvdParserTester(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(
      FindConfig(*serialized_data, R"(--report_anonymous_usage_stats=n)"))
      << "report_anonymous_usage_stats flag is missing or wrongly formatted";
}

}  // namespace cuttlefish
