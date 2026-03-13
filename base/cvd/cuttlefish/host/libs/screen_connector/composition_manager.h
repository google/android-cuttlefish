/*
 * Copyright (C) 2024 The Android Open Source Project
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
#pragma once

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "cuttlefish/host/libs/screen_connector/ring_buffer_manager.h"
#include "cuttlefish/host/libs/screen_connector/video_frame_buffer.h"
#include "cuttlefish/host/libs/wayland/wayland_server_callbacks.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
class DisplayHandler;

class CompositionManager {
 public:
  struct DisplayOverlay {
    int src_vm_index;
    int src_display_index;
  };

  ~CompositionManager();
  static Result<std::unique_ptr<CompositionManager>> Create();

  void OnDisplayCreated(const DisplayCreatedEvent& event);
  void OnFrame(uint32_t display_number, uint32_t frame_width,
               uint32_t frame_height, uint32_t frame_fourcc_format,
               uint32_t frame_stride_bytes, uint8_t* frame_pixels);

  void ComposeFrame(int display_index,
                    std::shared_ptr<VideoFrameBuffer> buffer);

 private:
  explicit CompositionManager(
      int cluster_index, std::string& group_uuid,
      std::map<int, std::vector<DisplayOverlay>>& overlays);

  class LastFrameInfo {
   public:
    LastFrameInfo() {}
    LastFrameInfo(uint32_t display_number, uint32_t frame_width,
                  uint32_t frame_height, uint32_t frame_fourcc_format,
                  uint32_t frame_stride_bytes, uint8_t* frame_pixels) {
      display_number_ = display_number;
      frame_width_ = frame_width;
      frame_height_ = frame_height;
      frame_fourcc_format_ = frame_fourcc_format;
      frame_stride_bytes_ = frame_stride_bytes;
      frame_pixels_ = frame_pixels;
    }
    uint32_t display_number_;
    uint32_t frame_width_;
    uint32_t frame_height_;
    uint32_t frame_fourcc_format_;
    uint32_t frame_stride_bytes_;
    uint8_t* frame_pixels_;
  };
  static std::map<int, std::vector<CompositionManager::DisplayOverlay>>
  ParseOverlays(std::vector<std::string> overlay_items);
  uint8_t* AlphaBlendLayers(uint8_t* frame_pixels, int display, int frame_width,
                            int frame_height);
  void ComposeFrame(int display, int width, int height,
                    uint32_t frame_fourcc_format, uint32_t frame_stride_bytes,
                    std::shared_ptr<VideoFrameBuffer> buffer);
  DisplayRingBufferManager display_ring_buffer_manager_;
  int cluster_index_;
  std::string group_uuid_;
  std::map<int, std::vector<DisplayOverlay>> cfg_overlays_;
  std::map<int, LastFrameInfo> last_frame_info_map_;
  std::map<int, std::vector<uint8_t>> frame_work_buffer_;
};

}  // namespace cuttlefish
