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

#include "cuttlefish/host/commands/assemble_cvd/guest_config.h"

#include <cstddef>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::Optional;
using ::testing::SizeIs;

bool WriteFile(const std::string& path, const std::string& contents) {
  std::ofstream out(path);
  out << contents;
  return out.good();
}

TEST(GuestConfigTest, ParsePresetFieldsFromTextProto) {
  const std::string path =
      testing::TempDir() + "/cuttlefish-guest-config-presets.txtpb";
  ASSERT_TRUE(WriteFile(path, R"(
device_type: Phone
graphics {
  displays {
    width: 450
    height: 450
    dpi: 160
  }
  displays {
    width: 1920
    height: 1080
    dpi: 140
    refresh_rate_hz: 90
  }
}
input {
  touchpads {
    width: 420
    height: 70
    type: TOUCHPAD_TYPE_TOUCH_NAVIGATION
  }
  touchpads {
    width: 1600
    height: 900
  }
}
storage {
  blank_data_image_mb: 16384
  use_sdcard: false
}
virtualization {
  memory_mb: 1536
}
services {
  vhal_proxy_server_enabled: true
}
)"));

  GuestConfig config;
  ASSERT_THAT(ParseGuestConfigTextProto(path, config), IsOk());

  ASSERT_THAT(config.displays, SizeIs(2));
  EXPECT_EQ(config.displays[0].width, 450);
  EXPECT_EQ(config.displays[0].height, 450);
  EXPECT_EQ(config.displays[0].dpi, 160);
  EXPECT_EQ(config.displays[0].refresh_rate_hz, 60);
  EXPECT_EQ(config.displays[1].width, 1920);
  EXPECT_EQ(config.displays[1].height, 1080);
  EXPECT_EQ(config.displays[1].dpi, 140);
  EXPECT_EQ(config.displays[1].refresh_rate_hz, 90);

  ASSERT_THAT(config.touchpads, SizeIs(2));
  EXPECT_EQ(config.touchpads[0].width, 420);
  EXPECT_EQ(config.touchpads[0].height, 70);
  EXPECT_EQ(config.touchpads[0].type,
            CuttlefishConfig::TouchpadConfig::Type::kTouchNavigation);
  EXPECT_EQ(config.touchpads[1].width, 1600);
  EXPECT_EQ(config.touchpads[1].height, 900);
  EXPECT_EQ(config.touchpads[1].type,
            CuttlefishConfig::TouchpadConfig::Type::kTouchpad);

  EXPECT_EQ(config.blank_data_image_mb, 16384);
  EXPECT_THAT(config.use_sdcard, Optional(false));
  EXPECT_THAT(config.memory_mb, Optional(1536));
  EXPECT_THAT(config.vhal_proxy_server_enabled, Optional(true));
}

TEST(GuestConfigTest, EmptyTextProtoLeavesPresetDefaultsUnset) {
  const std::string path =
      testing::TempDir() + "/cuttlefish-guest-config-empty.txtpb";
  ASSERT_TRUE(WriteFile(path, ""));

  GuestConfig config;
  ASSERT_THAT(ParseGuestConfigTextProto(path, config), IsOk());

  EXPECT_TRUE(config.displays.empty());
  EXPECT_TRUE(config.touchpads.empty());
  EXPECT_EQ(config.use_sdcard, std::nullopt);
  EXPECT_EQ(config.memory_mb, std::nullopt);
  EXPECT_EQ(config.vhal_proxy_server_enabled, std::nullopt);
}

TEST(GuestConfigTest, RejectsInvalidPresetValues) {
  const std::vector<std::string> invalid_configs = {
      "input { touchpads { width: 0 height: 70 } }",
      "graphics { displays { width: 450 height: 0 } }",
      "graphics { displays { width: 450 height: 450 dpi: 0 } }",
      "graphics { displays { width: 450 height: 450 refresh_rate_hz: -1 } }",
      "virtualization { memory_mb: -128 }",
  };

  for (size_t i = 0; i < invalid_configs.size(); ++i) {
    const std::string path = testing::TempDir() +
                             "/cuttlefish-guest-config-invalid-" +
                             std::to_string(i) + ".txtpb";
    ASSERT_TRUE(WriteFile(path, invalid_configs[i]));
    GuestConfig config;
    EXPECT_THAT(ParseGuestConfigTextProto(path, config), IsError())
        << "Expected error for: " << invalid_configs[i];
  }
}

}  // namespace
}  // namespace cuttlefish
