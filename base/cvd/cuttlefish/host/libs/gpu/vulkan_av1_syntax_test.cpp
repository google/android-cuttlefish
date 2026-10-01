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

#include "cuttlefish/host/libs/gpu/vulkan_av1_syntax.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "vk_video/vulkan_video_codec_av1std.h"
#include "vk_video/vulkan_video_codec_av1std_encode.h"
#include "vulkan/vulkan_core.h"

namespace cuttlefish {
namespace {

using ::testing::Each;

Av1PictureParams KeyFrameParams() {
  return Av1PictureParams{
      .order_hint = 0,
      .width = 1920,
      .height = 1080,
      .coded_extent = {1920, 1088},
  };
}

TEST(Av1PictureInfoTest, KeyFrameRefreshesEveryBuffer) {
  const Av1PictureInfo picture(KeyFrameParams());
  const StdVideoEncodeAV1PictureInfo& std_info =
      *picture.info().pStdPictureInfo;

  EXPECT_EQ(std_info.frame_type, STD_VIDEO_AV1_FRAME_TYPE_KEY);
  EXPECT_EQ(std_info.order_hint, 0);
  EXPECT_EQ(std_info.refresh_frame_flags, 0xff);
  EXPECT_THAT(std_info.ref_frame_idx, Each(-1));
  EXPECT_THAT(std_info.ref_order_hint, Each(0));
  EXPECT_EQ(picture.info().predictionMode,
            VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_INTRA_ONLY_KHR);
  EXPECT_THAT(picture.info().referenceNameSlotIndices, Each(-1));
}

TEST(Av1PictureInfoTest, PointsAtItsOwnStructures) {
  const Av1PictureInfo picture(KeyFrameParams());
  const StdVideoEncodeAV1PictureInfo& std_info =
      *picture.info().pStdPictureInfo;

  ASSERT_NE(std_info.pTileInfo, nullptr);
  EXPECT_EQ(std_info.pTileInfo->TileCols, 1);
  EXPECT_NE(std_info.pQuantization, nullptr);
  EXPECT_NE(std_info.pLoopFilter, nullptr);
  EXPECT_NE(std_info.pCDEF, nullptr);
}

TEST(Av1SequenceHeaderInfoTest, FrameSizeFieldsCoverTheCodedExtent) {
  const Av1SequenceHeaderInfo info(VkExtent2D{1920, 1088});
  const StdVideoAV1SequenceHeader& header = info.sequence_header();

  EXPECT_EQ(header.frame_width_bits_minus_1, 10);
  EXPECT_EQ(header.frame_height_bits_minus_1, 10);
  EXPECT_EQ(header.max_frame_width_minus_1, 1919);
  EXPECT_EQ(header.max_frame_height_minus_1, 1087);
  EXPECT_EQ(header.order_hint_bits_minus_1, kAv1OrderHintBits - 1);
  ASSERT_NE(header.pColorConfig, nullptr);
  EXPECT_EQ(header.pColorConfig->BitDepth, 8);
}

}  // namespace
}  // namespace cuttlefish
