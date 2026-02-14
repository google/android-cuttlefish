/*
 * Copyright (C) 2020 The Android Open Source Project
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

/*
 * TODO(b/384939093): PLEASE NOTE: The implemented here is in a WIP status.
 *
 * Currently the Composition algorithm implemented in
 * this module has a known limitation.  It uses IPC buffers in such a way where
 * it is currently possible for frames to be simultaneously
 * read and written from the same memory lcoation.  It's therefore possible to
 * have some display artifacts as partial frames are read.  To remedy there is
 * follow-up work (documented in b/384939093) planned.
 */

#include "cuttlefish/host/libs/screen_connector/composition_manager.h"

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include <android-base/parseint.h>
#include <android-base/strings.h>
#include "absl/log/log.h"
#include "libyuv.h"

#include <drm/drm_fourcc.h>
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/screen_connector/ring_buffer_manager.h"
#include "cuttlefish/host/libs/wayland/wayland_server_callbacks.h"

static const int kRedIdx = 0;
static const int kGreenIdx = 1;
static const int kBlueIdx = 2;
static const int kAlphaIdx = 3;

namespace cuttlefish {

void alpha_blend_layer(uint8_t* frame_pixels, uint32_t h, uint32_t w,
                       uint8_t* overlay) {
  uint8_t* dst = frame_pixels;
  uint8_t* src = overlay;
  int max = w * h;
  for (int idx = 0; idx < max; idx++) {
    float a = ((float)src[kAlphaIdx]) / 255.0f;
    float a_inv = 1.0f - a;
    dst[kRedIdx] = (uint8_t)((src[kRedIdx] * a) + (dst[kRedIdx] * a_inv));
    dst[kBlueIdx] = (uint8_t)((src[kBlueIdx] * a) + (dst[kBlueIdx] * a_inv));
    dst[kGreenIdx] = (uint8_t)((src[kGreenIdx] * a) + (dst[kGreenIdx] * a_inv));
    dst[kAlphaIdx] = 255;
    dst += 4;
    src += 4;
  }
}

std::map<int, std::vector<CompositionManager::DisplayOverlay>>
CompositionManager::ParseOverlays(std::vector<std::string> overlay_items) {
  std::map<int, std::vector<DisplayOverlay>> overlays;
  // This iterates the list of overlay tuples, entries are of the form x:y
  // where x is a vm index in the cluster, and y is a display
  // index within that vm.  Structured types are created as result.
  for (int display_index = 0; display_index < overlay_items.size();
       display_index++) {
    auto overlay_item = android::base::Trim(overlay_items[display_index]);

    if (overlay_item.empty() || overlay_item == "_") {
      continue;
    }

    std::vector<DisplayOverlay>& display_overlays = overlays[display_index];

    std::vector<std::string> overlay_list =
        android::base::Split(overlay_item, " ");

    for (const auto& overlay_tuple_str : overlay_list) {
      std::vector<std::string> overlay_tuple =
          android::base::Split(overlay_tuple_str, ":");

      DisplayOverlay docfg;

      if (overlay_tuple.size() == 2) {
        if (!(android::base::ParseInt(overlay_tuple[0], &docfg.src_vm_index) &&
              android::base::ParseInt(overlay_tuple[1],
                                      &docfg.src_display_index))) {
          LOG(FATAL) << "Failed to parse display overlay directive: "
                     << overlay_tuple_str;
        } else {
          display_overlays.push_back(docfg);
        }
      } else {
        LOG(FATAL) << "Failed to parse display overlay directive, not a tuple "
                      "of format x:y - "
                   << overlay_tuple_str;
      }
    }
  }
  return overlays;
}

CompositionManager::CompositionManager(
    int cluster_index, std::string& group_uuid,
    std::map<int, std::vector<DisplayOverlay>>& overlays)
    : display_ring_buffer_manager_(cluster_index - 1, group_uuid),
      cluster_index_(cluster_index - 1),
      group_uuid_(group_uuid),
      cfg_overlays_(overlays) {}

CompositionManager::~CompositionManager() {}

Result<std::unique_ptr<CompositionManager>> CompositionManager::Create() {
  auto cvd_config = CuttlefishConfig::Get();
  auto instance = cvd_config->ForDefaultInstance();
  // Aggregate all the display overlays into a single list per config
  std::vector<std::string> overlays;
  for (const auto& display : instance.display_configs()) {
    overlays.push_back(display.overlays);
  }

  std::map<int, std::vector<CompositionManager::DisplayOverlay>> domap =
      CompositionManager::ParseOverlays(overlays);
  for (auto const& [display_index, display_overlays] : domap) {
    for (auto const& display_overlay : display_overlays) {
      CF_EXPECTF(display_overlay.src_vm_index < cvd_config->Instances().size(),
                 "Invalid source overlay VM index: {}",
                 display_overlay.src_vm_index);

      const cuttlefish::CuttlefishConfig::InstanceSpecific src_instance =
          cvd_config->Instances()[display_overlay.src_vm_index];

      CF_EXPECTF(display_overlay.src_display_index <
                     src_instance.display_configs().size(),
                 "Invalid source overlay display index: {}",
                 display_overlay.src_vm_index);

      const cuttlefish::CuttlefishConfig::DisplayConfig src_display =
          src_instance.display_configs()[display_overlay.src_display_index];

      const cuttlefish::CuttlefishConfig::DisplayConfig dest_display =
          instance.display_configs()[display_index];

      CF_EXPECT(src_display.width == dest_display.width &&
                    src_display.height == dest_display.height,
                "Source and target overlay display must be of identical size.");
    }
  }

  // Calculate the instance's position within cluster
  // For display overlay config calculations
  int instance_index = instance.index();

  std::string group_uuid =
      fmt::format("{}", cvd_config->ForDefaultEnvironment().group_uuid());

  CF_EXPECT(!group_uuid.empty(), "Invalid group UUID");

  std::unique_ptr<CompositionManager> mgr(
      new CompositionManager(instance_index + 1, group_uuid, domap));

  return mgr;
}

// Whenever a display is created, a shared memory IPC ringbuffer
// is initialized so that other frames can obtain this display's contents
// for composition.
void CompositionManager::OnDisplayCreated(const DisplayCreatedEvent& e) {
  auto result = display_ring_buffer_manager_.CreateLocalDisplayBuffer(
      cluster_index_, e.display_number, e.display_width, e.display_height);

  if (!result.ok()) {
    LOG(FATAL) << "OnDisplayCreated failed: " << result.error();
  }
}

// Called every frame.
void CompositionManager::OnFrame(uint32_t display_number, uint32_t frame_width,
                                 uint32_t frame_height,
                                 uint32_t frame_fourcc_format,
                                 uint32_t frame_stride_bytes,
                                 uint8_t* frame_pixels) {
  // First step is to push the local display pixels to the shared memory region
  // ringbuffer
  uint8_t* shmem_local_display = display_ring_buffer_manager_.WriteFrame(
      cluster_index_, display_number, frame_pixels,
      frame_width * frame_height * 4);

  // Next some upkeep, the format of the frame is needed for blending
  // computations.
  LastFrameInfo last_frame_info = LastFrameInfo(
      display_number, frame_width, frame_height, frame_fourcc_format,
      frame_stride_bytes, (uint8_t*)shmem_local_display);

  last_frame_info_map_[display_number] = last_frame_info;

  // Lastly, the pixels of the current frame are modified by blending any
  // configured layers over the top of the current 'base layer'
  AlphaBlendLayers(frame_pixels, display_number, frame_width, frame_height);
}

// This is called to 'Force a Display Composition Refresh' on a display.  It is
// triggered by a thread to force displays to constantly update so that when
// layers are updated, the user will see the blended result.
void CompositionManager::ComposeFrame(
    int display_index, std::shared_ptr<VideoFrameBuffer> buffer) {
  if (!last_frame_info_map_.count(display_index)) {
    return;
  }
  LastFrameInfo& last_frame_info = last_frame_info_map_[display_index];

  ComposeFrame(display_index, last_frame_info.frame_width_,
               last_frame_info.frame_height_,
               last_frame_info.frame_fourcc_format_,
               last_frame_info.frame_stride_bytes_, buffer);
}

uint8_t* CompositionManager::AlphaBlendLayers(uint8_t* frame_pixels,
                                              int display_number,
                                              int frame_width,
                                              int frame_height) {
  if (cfg_overlays_.count(display_number) == 0) {
    return frame_pixels;
  }

  std::vector<DisplayOverlay>& cfg_overlays = cfg_overlays_[display_number];
  int num_overlays = cfg_overlays.size();

  std::vector<void*> overlays;
  overlays.resize(num_overlays, nullptr);

  for (int i = 0; i < num_overlays; i++) {
    if (overlays[i] != nullptr) {
      continue;
    }

    DisplayOverlay& layer = cfg_overlays[i];

    overlays[i] = display_ring_buffer_manager_.ReadFrame(
        layer.src_vm_index, layer.src_display_index, frame_width, frame_height);
  }

  for (auto i : overlays) {
    if (i) {
      alpha_blend_layer(frame_pixels, frame_height, frame_width, (uint8_t*)i);
    }
  }
  return (uint8_t*)frame_pixels;
}

void CompositionManager::ComposeFrame(
    int display, int width, int height, uint32_t frame_fourcc_format,
    uint32_t frame_stride_bytes, std::shared_ptr<VideoFrameBuffer> buffer) {
  uint8_t* shmem_local_display = display_ring_buffer_manager_.ReadFrame(
      cluster_index_, display, width, height);

  if (frame_work_buffer_.find(display) == frame_work_buffer_.end()) {
    frame_work_buffer_[display] = std::vector<uint8_t>(width * height * 4);
  }
  uint8_t* tmp_buffer = frame_work_buffer_[display].data();
  memcpy(tmp_buffer, shmem_local_display, width * height * 4);

  AlphaBlendLayers(tmp_buffer, display, width, height);

  if (frame_fourcc_format == DRM_FORMAT_ARGB8888 ||
      frame_fourcc_format == DRM_FORMAT_XRGB8888) {
    libyuv::ARGBToI420(tmp_buffer, frame_stride_bytes, buffer->DataY(),
                       buffer->StrideY(), buffer->DataU(), buffer->StrideU(),
                       buffer->DataV(), buffer->StrideV(), width, height);
  } else if (frame_fourcc_format == DRM_FORMAT_ABGR8888 ||
             frame_fourcc_format == DRM_FORMAT_XBGR8888) {
    libyuv::ABGRToI420(tmp_buffer, frame_stride_bytes, buffer->DataY(),
                       buffer->StrideY(), buffer->DataU(), buffer->StrideU(),
                       buffer->DataV(), buffer->StrideV(), width, height);
  }
}

}  // namespace cuttlefish
