/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/libs/config/media.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::Not;

TEST(MediaTest, ParseSingleInstanceSingleDevice) {
  std::vector<std::string> args = {
      "--media=v4l2_emulated_camera_splane:lens_facing=BACK"};
  const Result<std::vector<std::vector<CuttlefishConfig::MediaConfig>>>
      configs_res = ParseMediaConfigsFromArgs(args, /*num_instances=*/1);

  ASSERT_THAT(configs_res, IsOk());

  const std::vector<std::vector<CuttlefishConfig::MediaConfig>>& configs =
      *configs_res;

  ASSERT_EQ(configs.size(), 1);

  ASSERT_EQ(configs[0].size(), 1);
  EXPECT_EQ(configs[0][0].type,
            CuttlefishConfig::MediaType::kV4l2EmulatedCameraSPlane);
  EXPECT_EQ(configs[0][0].lens_facing, "BACK");
}

TEST(MediaTest, ParseSingleInstanceMultipleDevices) {
  std::vector<std::string> args = {
      "--media=v4l2_emulated_camera_splane:lens_facing=BACK",
      "--media=v4l2_emulated_camera_splane:lens_facing=FRONT"};
  const Result<std::vector<std::vector<CuttlefishConfig::MediaConfig>>>
      configs_res = ParseMediaConfigsFromArgs(args, /*num_instances=*/1);

  ASSERT_THAT(configs_res, IsOk());

  const std::vector<std::vector<CuttlefishConfig::MediaConfig>>& configs =
      *configs_res;

  ASSERT_EQ(configs.size(), 1);

  ASSERT_EQ(configs[0].size(), 2);
  EXPECT_EQ(configs[0][0].type,
            CuttlefishConfig::MediaType::kV4l2EmulatedCameraSPlane);
  EXPECT_EQ(configs[0][0].lens_facing, "BACK");
  EXPECT_EQ(configs[0][1].type,
            CuttlefishConfig::MediaType::kV4l2EmulatedCameraSPlane);
  EXPECT_EQ(configs[0][1].lens_facing, "FRONT");
}

TEST(MediaTest, ParseTwoInstancesBothWithDevices) {
  std::vector<std::string> args = {
      "--media=v4l2_emulated_camera_splane:lens_facing=BACK,"
      "v4l2_emulated_camera_splane:lens_facing=FRONT"};
  const Result<std::vector<std::vector<CuttlefishConfig::MediaConfig>>>
      configs_res = ParseMediaConfigsFromArgs(args, /*num_instances=*/2);

  ASSERT_THAT(configs_res, IsOk());

  const std::vector<std::vector<CuttlefishConfig::MediaConfig>>& configs =
      *configs_res;

  ASSERT_EQ(configs.size(), 2);

  ASSERT_EQ(configs[0].size(), 1);
  EXPECT_EQ(configs[0][0].type,
            CuttlefishConfig::MediaType::kV4l2EmulatedCameraSPlane);
  EXPECT_EQ(configs[0][0].lens_facing, "BACK");
  ASSERT_EQ(configs[1].size(), 1);
  EXPECT_EQ(configs[1][0].type,
            CuttlefishConfig::MediaType::kV4l2EmulatedCameraSPlane);
  EXPECT_EQ(configs[1][0].lens_facing, "FRONT");
}

TEST(MediaTest, ParseTwoInstancesOnlyFirstWithDevice) {
  std::vector<std::string> args = {
      "--media=v4l2_emulated_camera_splane:lens_facing=BACK,"};
  const Result<std::vector<std::vector<CuttlefishConfig::MediaConfig>>>
      configs_res = ParseMediaConfigsFromArgs(args, /*num_instances=*/2);

  ASSERT_THAT(configs_res, IsOk());

  const std::vector<std::vector<CuttlefishConfig::MediaConfig>>& configs =
      *configs_res;

  ASSERT_EQ(configs.size(), 2);

  ASSERT_EQ(configs[0].size(), 1);
  EXPECT_EQ(configs[0][0].type,
            CuttlefishConfig::MediaType::kV4l2EmulatedCameraSPlane);
  EXPECT_EQ(configs[0][0].lens_facing, "BACK");

  EXPECT_TRUE(configs[1].empty());
}

TEST(MediaTest, ParseTwoInstancesOnlySecondWithDevice) {
  std::vector<std::string> args = {
      "--media=,v4l2_emulated_camera_splane:lens_facing=FRONT"};
  const Result<std::vector<std::vector<CuttlefishConfig::MediaConfig>>>
      configs_res = ParseMediaConfigsFromArgs(args, /*num_instances=*/2);
  ASSERT_THAT(configs_res, IsOk());
  const std::vector<std::vector<CuttlefishConfig::MediaConfig>>& configs =
      *configs_res;
  ASSERT_EQ(configs.size(), 2);

  EXPECT_TRUE(configs[0].empty());

  ASSERT_EQ(configs[1].size(), 1);
  EXPECT_EQ(configs[1][0].type,
            CuttlefishConfig::MediaType::kV4l2EmulatedCameraSPlane);
  EXPECT_EQ(configs[1][0].lens_facing, "FRONT");
}

TEST(MediaTest, ParseTwoInstancesMultipleRepeatedFlags) {
  std::vector<std::string> args = {
      "--media=v4l2_emulated_camera_splane:lens_facing=BACK,"
      "v4l2_emulated_camera_splane:lens_facing=BACK",
      "--media=v4l2_emulated_camera_splane:lens_facing=FRONT,"
      "v4l2_emulated_camera_splane:lens_facing=FRONT"};
  const Result<std::vector<std::vector<CuttlefishConfig::MediaConfig>>>
      configs_res = ParseMediaConfigsFromArgs(args, /*num_instances=*/2);

  ASSERT_THAT(configs_res, IsOk());

  const std::vector<std::vector<CuttlefishConfig::MediaConfig>>& configs =
      *configs_res;

  ASSERT_EQ(configs.size(), 2);

  ASSERT_EQ(configs[0].size(), 2);
  EXPECT_EQ(configs[0][0].type,
            CuttlefishConfig::MediaType::kV4l2EmulatedCameraSPlane);
  EXPECT_EQ(configs[0][0].lens_facing, "BACK");
  EXPECT_EQ(configs[0][1].type,
            CuttlefishConfig::MediaType::kV4l2EmulatedCameraSPlane);
  EXPECT_EQ(configs[0][1].lens_facing, "FRONT");

  ASSERT_EQ(configs[1].size(), 2);
  EXPECT_EQ(configs[1][0].type,
            CuttlefishConfig::MediaType::kV4l2EmulatedCameraSPlane);
  EXPECT_EQ(configs[1][0].lens_facing, "BACK");
  EXPECT_EQ(configs[1][1].type,
            CuttlefishConfig::MediaType::kV4l2EmulatedCameraSPlane);
  EXPECT_EQ(configs[1][1].lens_facing, "FRONT");
}

TEST(MediaTest, RejectMismatchedCommasWhenNumInstancesDefined) {
  std::vector<std::string> args = {
      "--media=v4l2_emulated_camera_splane:lens_facing=BACK"};

  const Result<std::vector<std::vector<CuttlefishConfig::MediaConfig>>>
      configs_res = ParseMediaConfigsFromArgs(args, /*num_instances=*/2);

  EXPECT_THAT(configs_res, Not(IsOk()));
}

TEST(MediaTest, RejectCommasWhenSingleInstance) {
  std::vector<std::string> args = {
      "--media=v4l2_emulated_camera_splane:lens_facing=BACK,"
      "v4l2_emulated_camera_splane:lens_facing=FRONT"};

  const Result<std::vector<std::vector<CuttlefishConfig::MediaConfig>>>
      configs_res = ParseMediaConfigsFromArgs(args, /*num_instances=*/1);

  EXPECT_THAT(configs_res, Not(IsOk()));
}

TEST(MediaTest, RejectDifferingCommaCountsAcrossFlags) {
  std::vector<std::string> args = {
      "--media=v4l2_emulated_camera_splane:lens_facing=BACK,"
      "v4l2_emulated_camera_splane:lens_facing=BACK",
      "--media=v4l2_emulated_camera_splane:lens_facing=FRONT"};

  const Result<std::vector<std::vector<CuttlefishConfig::MediaConfig>>>
      configs_res = ParseMediaConfigsFromArgs(args, /*num_instances=*/2);

  EXPECT_THAT(configs_res, Not(IsOk()));
}

TEST(MediaTest, RejectNonPositiveNumInstances) {
  std::vector<std::string> args = {
      "--media=v4l2_emulated_camera_splane:lens_facing=BACK"};

  EXPECT_THAT(ParseMediaConfigsFromArgs(args, /*num_instances=*/0),
              Not(IsOk()));

  EXPECT_THAT(ParseMediaConfigsFromArgs(args, /*num_instances=*/-1),
              Not(IsOk()));
}

}  // namespace
}  // namespace cuttlefish
