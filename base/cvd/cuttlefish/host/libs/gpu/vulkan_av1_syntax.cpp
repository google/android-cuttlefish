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

#include <stdint.h>

#include <algorithm>
#include <bit>

#include "vk_video/vulkan_video_codec_av1std.h"
#include "vk_video/vulkan_video_codec_av1std_encode.h"
#include "vulkan/vulkan_core.h"

namespace cuttlefish {
namespace {

struct LevelLimit {
  uint64_t max_picture_size;
  StdVideoAV1Level level;
};

constexpr LevelLimit kAv1LevelLimits[] = {
    {147456, STD_VIDEO_AV1_LEVEL_2_0},   {278784, STD_VIDEO_AV1_LEVEL_2_1},
    {665856, STD_VIDEO_AV1_LEVEL_3_0},   {1065024, STD_VIDEO_AV1_LEVEL_3_1},
    {2359296, STD_VIDEO_AV1_LEVEL_4_0},  {8912896, STD_VIDEO_AV1_LEVEL_5_0},
    {35651584, STD_VIDEO_AV1_LEVEL_6_0},
};

// The lowest AV1 level whose MaxPicSize (AV1 specification, Annex A) covers
// the coded picture. The driver may override this in the sequence header it
// generates, which is why the bytes it returns are the ones emitted.
StdVideoAV1Level GuessLevel(uint32_t width, uint32_t height) {
  const uint64_t picture_size = static_cast<uint64_t>(width) * height;
  for (const LevelLimit& limit : kAv1LevelLimits) {
    if (picture_size <= limit.max_picture_size) {
      return limit.level;
    }
  }
  return STD_VIDEO_AV1_LEVEL_7_3;
}

uint8_t BitsMinusOne(uint32_t max_value) {
  return static_cast<uint8_t>(std::bit_width(max_value) - 1);
}

// BT.709 limited range, fixed here so the color conversion and the
// signalled bitstream agree.
StdVideoAV1ColorConfig Bt709ColorConfig() {
  return StdVideoAV1ColorConfig{
      .flags =
          {
              .mono_chrome = 0,
              .color_range = 0,
              .separate_uv_delta_q = 0,
              .color_description_present_flag = 1,
          },
      .BitDepth = 8,
      .subsampling_x = 1,
      .subsampling_y = 1,
      .reserved1 = 0,
      .color_primaries = STD_VIDEO_AV1_COLOR_PRIMARIES_BT_709,
      .transfer_characteristics = STD_VIDEO_AV1_TRANSFER_CHARACTERISTICS_BT_709,
      .matrix_coefficients = STD_VIDEO_AV1_MATRIX_COEFFICIENTS_BT_709,
      .chroma_sample_position = STD_VIDEO_AV1_CHROMA_SAMPLE_POSITION_UNKNOWN,
  };
}

StdVideoAV1SequenceHeaderFlags SequenceHeaderFlags() {
  return StdVideoAV1SequenceHeaderFlags{
      .still_picture = 0,
      .reduced_still_picture_header = 0,
      .use_128x128_superblock = 0,
      // Some encoders use neither intra tool and a driver may still return
      // these flags as requested, which leaves decoders expecting syntax the
      // frames do not carry.
      .enable_filter_intra = 0,
      .enable_intra_edge_filter = 0,
      .enable_interintra_compound = 0,
      .enable_masked_compound = 0,
      .enable_warped_motion = 0,
      .enable_dual_filter = 0,
      .enable_order_hint = 1,
      .enable_jnt_comp = 0,
      .enable_ref_frame_mvs = 0,
      .frame_id_numbers_present_flag = 0,
      .enable_superres = 0,
      .enable_cdef = 1,
      .enable_restoration = 0,
      .film_grain_params_present = 0,
      .timing_info_present_flag = 0,
      .initial_display_delay_present_flag = 0,
  };
}

StdVideoAV1SequenceHeader SequenceHeader(
    VkExtent2D coded_extent, const StdVideoAV1ColorConfig* color_config,
    const StdVideoAV1TimingInfo* timing_info) {
  return StdVideoAV1SequenceHeader{
      .flags = SequenceHeaderFlags(),
      .seq_profile = STD_VIDEO_AV1_PROFILE_MAIN,
      .frame_width_bits_minus_1 = BitsMinusOne(coded_extent.width - 1),
      .frame_height_bits_minus_1 = BitsMinusOne(coded_extent.height - 1),
      .max_frame_width_minus_1 = static_cast<uint16_t>(coded_extent.width - 1),
      .max_frame_height_minus_1 =
          static_cast<uint16_t>(coded_extent.height - 1),
      .delta_frame_id_length_minus_2 = 0,
      .additional_frame_id_length_minus_1 = 0,
      .order_hint_bits_minus_1 = kAv1OrderHintBits - 1,
      .seq_force_integer_mv = STD_VIDEO_AV1_SELECT_INTEGER_MV,
      .seq_force_screen_content_tools =
          STD_VIDEO_AV1_SELECT_SCREEN_CONTENT_TOOLS,
      .reserved1 = {},
      .pColorConfig = color_config,
      .pTimingInfo = timing_info,
  };
}

StdVideoEncodeAV1OperatingPointInfo OperatingPoint(VkExtent2D coded_extent) {
  return StdVideoEncodeAV1OperatingPointInfo{
      .flags =
          {
              .decoder_model_present_for_this_op = 0,
              .low_delay_mode_flag = 1,
              .initial_display_delay_present_for_this_op = 0,
          },
      .operating_point_idc = 0,
      .seq_level_idx = static_cast<uint8_t>(
          GuessLevel(coded_extent.width, coded_extent.height)),
      .seq_tier = 0,
      .decoder_buffer_delay = 0,
      .encoder_buffer_delay = 0,
      .initial_display_delay_minus_1 = 0,
  };
}

constexpr uint8_t kRefreshAllFrames = (1u << STD_VIDEO_AV1_NUM_REF_FRAMES) - 1;

StdVideoAV1TileInfo SingleTileInfo() {
  return StdVideoAV1TileInfo{
      .flags = {.uniform_tile_spacing_flag = 1},
      .TileCols = 1,
      .TileRows = 1,
      .context_update_tile_id = 0,
      .tile_size_bytes_minus_1 = 0,
      .reserved1 = {},
      .pMiColStarts = nullptr,
      .pMiRowStarts = nullptr,
      .pWidthInSbsMinus1 = nullptr,
      .pHeightInSbsMinus1 = nullptr,
  };
}

StdVideoAV1Quantization Quantization(uint32_t q_index) {
  return StdVideoAV1Quantization{
      .flags = {.using_qmatrix = 0, .diff_uv_delta = 0},
      .base_q_idx = static_cast<uint8_t>(q_index),
      .DeltaQYDc = 0,
      .DeltaQUDc = 0,
      .DeltaQUAc = 0,
      .DeltaQVDc = 0,
      .DeltaQVAc = 0,
      .qm_y = 0,
      .qm_u = 0,
      .qm_v = 0,
  };
}

StdVideoEncodeAV1PictureInfoFlags PictureFlags(const Av1PictureParams& params) {
  const bool padded = params.coded_extent.width != params.width ||
                      params.coded_extent.height != params.height;
  return StdVideoEncodeAV1PictureInfoFlags{
      .error_resilient_mode = 1,
      .disable_cdf_update = 0,
      .use_superres = 0,
      .render_and_frame_size_different = padded ? 1u : 0u,
      .allow_screen_content_tools = 0,
      .is_filter_switchable = 0,
      .force_integer_mv = 0,
      .frame_size_override_flag = 0,
      .buffer_removal_time_present_flag = 0,
      .allow_intrabc = 0,
      .frame_refs_short_signaling = 0,
      .allow_high_precision_mv = 0,
      .is_motion_mode_switchable = 0,
      .use_ref_frame_mvs = 0,
      .disable_frame_end_update_cdf = 0,
      .allow_warped_motion = 0,
      .reduced_tx_set = 0,
      .skip_mode_present = 0,
      .delta_q_present = 0,
      .delta_lf_present = 0,
      .delta_lf_multi = 0,
      .segmentation_enabled = 0,
      .segmentation_update_map = 0,
      .segmentation_temporal_update = 0,
      .segmentation_update_data = 0,
      .UsesLr = 0,
      .usesChromaLr = 0,
      .show_frame = 1,
      .showable_frame = params.key_frame ? 0u : 1u,
  };
}

// A key frame refreshes every reference buffer; an inter frame refreshes the
// one buffer that shares its index with the slot it reconstructs into.
uint8_t RefreshFrameFlags(const Av1PictureParams& params) {
  return params.key_frame ? kRefreshAllFrames
                          : static_cast<uint8_t>(1u << params.setup_slot);
}

StdVideoEncodeAV1PictureInfo StdPictureInfo(
    const Av1PictureParams& params, const StdVideoAV1TileInfo* tile_info,
    const StdVideoAV1Quantization* quantization,
    const StdVideoAV1LoopFilter* loop_filter, const StdVideoAV1CDEF* cdef) {
  StdVideoEncodeAV1PictureInfo picture_info = {
      .flags = PictureFlags(params),
      .frame_type = params.key_frame ? STD_VIDEO_AV1_FRAME_TYPE_KEY
                                     : STD_VIDEO_AV1_FRAME_TYPE_INTER,
      .frame_presentation_time = 0,
      .current_frame_id = 0,
      .order_hint = params.order_hint,
      // Error resilient mode leaves no primary reference, so every frame
      // starts from the default probabilities.
      .primary_ref_frame = STD_VIDEO_AV1_PRIMARY_REF_NONE,
      .refresh_frame_flags = RefreshFrameFlags(params),
      .coded_denom = 0,
      .render_width_minus_1 = static_cast<uint16_t>(params.width - 1),
      .render_height_minus_1 = static_cast<uint16_t>(params.height - 1),
      .interpolation_filter = STD_VIDEO_AV1_INTERPOLATION_FILTER_EIGHTTAP,
      .TxMode = STD_VIDEO_AV1_TX_MODE_SELECT,
      .delta_q_res = 0,
      .delta_lf_res = 0,
      .ref_order_hint = {},
      .ref_frame_idx = {-1, -1, -1, -1, -1, -1, -1},
      .reserved1 = {},
      .delta_frame_id_minus_1 = {},
      .pTileInfo = tile_info,
      .pQuantization = quantization,
      .pSegmentation = nullptr,
      .pLoopFilter = loop_filter,
      .pCDEF = cdef,
      .pLoopRestoration = nullptr,
      .pGlobalMotion = nullptr,
      .pExtensionHeader = nullptr,
      .pBufferRemovalTimes = nullptr,
  };
  if (params.reference_slot.has_value()) {
    std::ranges::copy(params.ref_order_hints, picture_info.ref_order_hint);
    // Every reference name resolves to the one buffer this frame predicts
    // from, which is the only one the driver is allowed to use.
    std::ranges::fill(picture_info.ref_frame_idx,
                      static_cast<int8_t>(*params.reference_slot));
  }
  return picture_info;
}

VkVideoEncodeAV1PictureInfoKHR EncodePictureInfo(
    const Av1PictureParams& params,
    const StdVideoEncodeAV1PictureInfo* std_picture_info) {
  VkVideoEncodeAV1PictureInfoKHR picture_info = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PICTURE_INFO_KHR,
      .pNext = nullptr,
      .predictionMode =
          params.key_frame
              ? VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_INTRA_ONLY_KHR
              : VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_SINGLE_REFERENCE_KHR,
      .rateControlGroup =
          params.key_frame
              ? VK_VIDEO_ENCODE_AV1_RATE_CONTROL_GROUP_INTRA_KHR
              : VK_VIDEO_ENCODE_AV1_RATE_CONTROL_GROUP_PREDICTIVE_KHR,
      .constantQIndex = params.constant_q_index,
      .pStdPictureInfo = std_picture_info,
      .referenceNameSlotIndices = {-1, -1, -1, -1, -1, -1, -1},
      .primaryReferenceCdfOnly = VK_FALSE,
      .generateObuExtensionHeader = VK_FALSE,
  };
  if (params.reference_slot.has_value()) {
    picture_info.referenceNameSlotIndices[0] = *params.reference_slot;
  }
  return picture_info;
}

StdVideoEncodeAV1ReferenceInfo ReferenceInfo(StdVideoAV1FrameType frame_type,
                                             uint8_t order_hint) {
  return StdVideoEncodeAV1ReferenceInfo{
      .flags = {.disable_frame_end_update_cdf = 0, .segmentation_enabled = 0},
      .RefFrameId = 0,
      .frame_type = frame_type,
      .OrderHint = order_hint,
      .reserved1 = {},
      .pExtensionHeader = nullptr,
  };
}

VkVideoEncodeAV1DpbSlotInfoKHR DpbSlotInfo(
    const StdVideoEncodeAV1ReferenceInfo* reference_info) {
  return VkVideoEncodeAV1DpbSlotInfoKHR{
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_DPB_SLOT_INFO_KHR,
      .pNext = nullptr,
      .pStdReferenceInfo = reference_info,
  };
}

}  // namespace

Av1SequenceHeaderInfo::Av1SequenceHeaderInfo(VkExtent2D coded_extent)
    : color_config_(Bt709ColorConfig()),
      timing_info_{},
      sequence_header_(
          SequenceHeader(coded_extent, &color_config_, &timing_info_)),
      operating_point_(OperatingPoint(coded_extent)) {}

Av1PictureInfo::Av1PictureInfo(const Av1PictureParams& params)
    : tile_info_(SingleTileInfo()),
      quantization_(Quantization(params.q_index)),
      loop_filter_{},
      cdef_{},
      picture_info_(StdPictureInfo(params, &tile_info_, &quantization_,
                                   &loop_filter_, &cdef_)),
      av1_picture_info_(EncodePictureInfo(params, &picture_info_)),
      setup_reference_info_(
          ReferenceInfo(picture_info_.frame_type, params.order_hint)),
      setup_dpb_slot_info_(DpbSlotInfo(&setup_reference_info_)),
      reference_info_(ReferenceInfo(params.reference_frame_type,
                                    params.reference_order_hint)),
      reference_dpb_slot_info_(DpbSlotInfo(&reference_info_)) {}

}  // namespace cuttlefish
