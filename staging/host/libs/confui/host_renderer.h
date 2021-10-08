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

#include <freetype/ftglyph.h>  // $(croot)/external/freetype
#include <teeui/utils.h>       // $(croot)/system/teeui/libteeui/.../include

#include "common/libs/confui/confui.h"
#include "host/libs/confui/layouts/layout.h"
#include "host/libs/confui/server_common.h"
#include "host/libs/screen_connector/screen_connector_common.h"

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

/**
 * create a raw frame for confirmation UI dialog
 *
 * Many rendering code borrowed from the following source
 *  https://android.googlesource.com/trusty/app/confirmationui/+/0429cc7/src
 */
class ConfUiRenderer {
 public:
  using LabelConfMsg = teeui::LabelBody;

  static std::unique_ptr<ConfUiRenderer> GenerateRenderer(
      const std::uint32_t display, const std::string& confirmation_msg,
      const std::string& locale, const bool inverted, const bool magnified);

  /**
   * this does not repaint from the scratch all the time
   *
   * It does repaint its frame buffer only when w/h of
   * current display has changed
   */
  std::shared_ptr<TeeUiFrameWrapper> RenderRawFrame();

  bool IsFrameReady() const { return raw_frame_ && !raw_frame_->IsEmpty(); }

  bool IsInConfirm(const std::uint32_t x, const std::uint32_t y) {
    return IsInside<teeui::LabelOK>(x, y);
  }
  bool IsInCancel(const std::uint32_t x, const std::uint32_t y) {
    return IsInside<teeui::LabelCancel>(x, y);
  }

 private:
  bool IsSetUpSuccessful() const { return is_setup_well_; }
  ConfUiRenderer(const std::uint32_t display,
                 const std::string& confirmation_msg, const std::string& locale,
                 const bool inverted, const bool magnified);

  struct Boundary {            // inclusive but.. LayoutElement's size is float
    std::uint32_t x, y, w, h;  // (x, y) is the top left
  };

  template <typename LayoutElement>
  Boundary GetBoundary(LayoutElement&& e) const {
    auto box = e.bounds_;
    Boundary b;
    // (x,y) is left top. so floor() makes sense
    // w, h are witdh and height in float. perhaps ceiling makes more
    // sense
    b.x = static_cast<std::uint32_t>(box.x().floor().count());
    b.y = static_cast<std::uint32_t>(box.y().floor().count());
    b.w = static_cast<std::uint32_t>(box.w().ceil().count());
    b.h = static_cast<std::uint32_t>(box.h().ceil().count());
    return b;
  }

  template <typename Element>
  bool IsInside(const std::uint32_t x, const std::uint32_t y) const {
    auto box = GetBoundary(std::get<Element>(layout_));
    if (x >= box.x && x <= box.x + box.w && y >= box.y && y <= box.y + box.h) {
      return true;
    }
    return false;
  }
  // essentially, to repaint from the scratch, so returns new frame
  // when successful. Or, nullopt
  std::unique_ptr<TeeUiFrameWrapper> RepaintRawFrame(const int w, const int h);

  bool InitLayout(const std::string& lang_id);
  teeui::Error UpdateTranslations();
  teeui::Error UpdateLocale();
  void SetDeviceContext(const unsigned long long w, const unsigned long long h,
                        bool is_inverted, bool is_magnified);

  // a callback function to be effectively sent to TeeUI library
  teeui::Error UpdatePixels(TeeUiFrameWrapper& buffer, std::uint32_t x,
                            std::uint32_t y, teeui::Color color);

  // second param is for type deduction
  template <typename... Elements>
  static teeui::Error drawElements(std::tuple<Elements...>& layout,
                                   const teeui::PixelDrawer& drawPixel) {
    // Error::operator|| is overloaded, so we don't get short circuit
    // evaluation. But we get the first error that occurs. We will still try and
    // draw the remaining elements in the order they appear in the layout tuple.
    return (std::get<Elements>(layout).draw(drawPixel) || ...);
  }
  void UpdateColorScheme(const bool is_inverted);
  template <typename Label>
  auto SetText(const std::string& text) {
    return std::get<Label>(layout_).setText(
        {text.c_str(), text.c_str() + text.size()});
  }

  template <typename Label>
  teeui::Error UpdateString();

  std::uint32_t display_num_;
  teeui::layout_t<teeui::ConfUILayout> layout_;
  std::string lang_id_;
  std::string prompt_text_;  // confirmation ui message

  /**
   * Potentially, the same frame could be requested multiple times.
   *
   * While another thread/caller is using this frame, the frame should
   * be kept here, too, to be returned upon future requests.
   *
   */
  std::shared_ptr<TeeUiFrameWrapper> raw_frame_;
  std::uint32_t current_height_;
  std::uint32_t current_width_;
  teeui::Color color_bg_;
  teeui::Color color_text_;
  teeui::Color shield_color_;
  bool is_inverted_;
  bool is_magnified_;
  teeui::context<teeui::ConfUIParameters> ctx_;
  bool is_setup_well_;

  static constexpr const teeui::Color kColorBackground = 0xffffffff;
  static constexpr const teeui::Color kColorBackgroundInv = 0xff212121;
  static constexpr const teeui::Color kColorDisabled = 0xffbdbdbd;
  static constexpr const teeui::Color kColorDisabledInv = 0xff424242;
  static constexpr const teeui::Color kColorEnabled = 0xff212121;
  static constexpr const teeui::Color kColorEnabledInv = 0xffdedede;
  static constexpr const teeui::Color kColorShield = 0xff778500;
  static constexpr const teeui::Color kColorShieldInv = 0xffc4cb80;
  static constexpr const teeui::Color kColorText = 0xff212121;
  static constexpr const teeui::Color kColorTextInv = 0xffdedede;
};
}  // end of namespace confui
}  // end of namespace cuttlefish
