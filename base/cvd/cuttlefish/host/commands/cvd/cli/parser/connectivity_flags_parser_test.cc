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

#include <string>

#include "gtest/gtest.h"

#include "cuttlefish/host/commands/cvd/cli/parser/test_common.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(ConnectivityFlagsParserTest, ParseModemSimulatorSimTypeValidInt) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "connectivity": {
            "modem_simulator_sim_type": 2
          }
        },
        {
          "connectivity": {
          }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  EXPECT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(FindConfig(*serialized_data, "--modem_simulator_sim_type=2,1"))
      << "modem_simulator_sim_type flag is missing or wrongly formatted";
}

TEST(ConnectivityFlagsParserTest, ParseModemSimulatorSimTypeValidString) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "connectivity": {
            "modem_simulator_sim_type": "MODEM_SIMULATOR_SIM_TYPE_CTS_CARRIER_API"
          }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  EXPECT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(FindConfig(*serialized_data, "--modem_simulator_sim_type=2"))
      << "modem_simulator_sim_type flag is missing or wrongly formatted";
}

TEST(ConnectivityFlagsParserTest, ParseModemSimulatorSimTypeUnspecified) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "connectivity": {
            "modem_simulator_sim_type": "MODEM_SIMULATOR_SIM_TYPE_UNSPECIFIED"
          }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  EXPECT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(FindConfig(*serialized_data, "--modem_simulator_sim_type=1"))
      << "modem_simulator_sim_type flag is missing or wrongly formatted";
}

TEST(ConnectivityFlagsParserTest, ParseModemSimulatorSimTypeInvalidString) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "connectivity": {
            "modem_simulator_sim_type": "INVALID_VALUE"
          }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  EXPECT_FALSE(serialized_data.has_value());
}

TEST(ConnectivityFlagsParserTest, ParseModemSimulatorSimTypeInvalidInt) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "connectivity": {
            "modem_simulator_sim_type": 3
          }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  EXPECT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(FindConfig(*serialized_data, "--modem_simulator_sim_type=1"))
      << "modem_simulator_sim_type flag is missing or wrongly formatted";
}

}  // namespace
}  // namespace cuttlefish
