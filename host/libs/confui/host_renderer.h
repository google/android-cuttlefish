/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <android-base/logging.h>
#include <freetype/ftglyph.h>  // $(croot)/external/freetype
#include <fruit/fruit.h>
#include <teeui/utils.h>       // $(croot)/system/teeui/libteeui/.../include

#include "common/libs/confui/confui.h"
#include "common/libs/utils/result.h"
#include "host/libs/confui/layouts/layout.h"
#include "host/libs/confui/server_common.h"
#include "host/libs/screen_connector/screen_connector.h"

namespace cuttlefish {
namespace confui {
class TeeUiFrameWrapper {
 public:
  TeeUiFrameWrapper(const int w, const int h, const teeui::Color color)
      : w_(w), h_(h), teeui_frame_(ScreenSizeInBytes(w, h), color) {}
  TeeUiFrameWrapper() = delete;
  auto data() { return teeui_frame_.data(); }
  int Width() const { return w_; }
  int Height() const { return h_; }
  bool IsEmpty() const { return teeui_frame_.empty(); }
  auto Size() const { return teeui_frame_.size(); }
  auto& operator[](const int idx) { return teeui_frame_[idx]; }
  std::uint32_t ScreenStrideBytes() const {
    return ScreenConnectorInfo::ComputeScreenStrideBytes(w_);
  }

 private:
  static std::uint32_t ScreenSizeInBytes(const int w, const int h) {
    return ScreenConnectorInfo::ComputeScreenSizeInBytes(w, h);
  }

  int w_;
  int h_;
  TeeUiFrame teeui_frame_;
};

class ConfUiRendererImpl;
class ConfUiRenderer {
 public:
  INJECT(ConfUiRenderer(ScreenConnectorFrameRenderer& screen_connector));
  ~ConfUiRenderer();
  Result<void> RenderDialog(const std::uint32_t display_num,
                            const std::string& prompt_text,
                            const std::string& locale,
                            const std::vector<teeui::UIOption>& ui_options);
  bool IsInConfirm(const std::uint32_t x, const std::uint32_t y);
  bool IsInCancel(const std::uint32_t x, const std::uint32_t y);

 private:
  bool IsInverted(const std::vector<teeui::UIOption>& ui_options) const;
  bool IsMagnified(const std::vector<teeui::UIOption>& ui_options) const;
  ScreenConnectorFrameRenderer& screen_connector_;
  std::unique_ptr<ConfUiRendererImpl> renderer_impl_;
};

}  // end of namespace confui
}  // end of namespace cuttlefish
