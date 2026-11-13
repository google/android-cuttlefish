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

#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "vk_video/vulkan_video_codec_av1std.h"
#include "vk_video/vulkan_video_codec_av1std_encode.h"
#include "vulkan/vulkan_core.h"

namespace cuttlefish {
namespace {

using ::testing::Each;
using ::testing::ElementsAreArray;

Av1PictureParams KeyFrameParams() {
  return Av1PictureParams{
      .key_frame = true,
      .order_hint = 0,
      .setup_slot = 0,
      .reference_slot = std::nullopt,
      .width = 1920,
      .height = 1080,
      .coded_extent = {1920, 1088},
  };
}

Av1PictureParams InterFrameParams() {
  return Av1PictureParams{
      .key_frame = false,
      .order_hint = 7,
      .setup_slot = 1,
      .reference_slot = 0,
      .width = 1920,
      .height = 1080,
      .coded_extent = {1920, 1088},
      .ref_order_hints = {6, 6, 6, 6, 6, 6, 6, 6},
      .reference_frame_type = STD_VIDEO_AV1_FRAME_TYPE_INTER,
      .reference_order_hint = 6,
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

TEST(Av1PictureInfoTest, InterFrameRefreshesItsSetupSlotOnly) {
  const Av1PictureParams params = InterFrameParams();
  const Av1PictureInfo picture(params);
  const StdVideoEncodeAV1PictureInfo& std_info =
      *picture.info().pStdPictureInfo;

  EXPECT_EQ(std_info.frame_type, STD_VIDEO_AV1_FRAME_TYPE_INTER);
  EXPECT_EQ(std_info.order_hint, 7);
  EXPECT_EQ(std_info.refresh_frame_flags, 1u << 1);
  EXPECT_THAT(std_info.ref_frame_idx, Each(0));
  EXPECT_THAT(std_info.ref_order_hint,
              ElementsAreArray(params.ref_order_hints));
  EXPECT_EQ(picture.info().predictionMode,
            VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_SINGLE_REFERENCE_KHR);
  EXPECT_EQ(picture.info().referenceNameSlotIndices[0], 0);
}

TEST(Av1PictureInfoTest, SlotInfoCarriesFrameTypeAndOrderHint) {
  const Av1PictureInfo picture(InterFrameParams());

  const StdVideoEncodeAV1ReferenceInfo& setup =
      *picture.setup_slot_info().pStdReferenceInfo;
  EXPECT_EQ(setup.frame_type, STD_VIDEO_AV1_FRAME_TYPE_INTER);
  EXPECT_EQ(setup.OrderHint, 7);

  const StdVideoEncodeAV1ReferenceInfo& reference =
      *picture.reference_slot_info().pStdReferenceInfo;
  EXPECT_EQ(reference.frame_type, STD_VIDEO_AV1_FRAME_TYPE_INTER);
  EXPECT_EQ(reference.OrderHint, 6);
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
