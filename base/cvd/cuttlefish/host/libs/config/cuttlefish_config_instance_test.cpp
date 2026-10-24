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

#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

TEST(CuttlefishConfigInstanceTest, FrameRepeatIntervalDefaultsWhenUnset) {
  // A config written before the setting existed has no key for it.
  CuttlefishConfig config;
  EXPECT_EQ(
      std::as_const(config).ForInstance(1).webrtc_frame_repeat_interval_ms(),
      20);
}

TEST(CuttlefishConfigInstanceTest, FrameRepeatIntervalSurvivesSaveAndLoad) {
  CuttlefishConfig config;
  config.ForInstance(1).set_webrtc_frame_repeat_interval_ms(500);

  const std::string path =
      ::testing::TempDir() + "/frame_repeat_interval_config.json";
  ASSERT_TRUE(config.SaveToFile(path));

  CuttlefishConfig loaded;
  ASSERT_TRUE(loaded.LoadFromFile(path.c_str()));
  EXPECT_EQ(
      std::as_const(loaded).ForInstance(1).webrtc_frame_repeat_interval_ms(),
      500);
}

}  // namespace
}  // namespace cuttlefish
