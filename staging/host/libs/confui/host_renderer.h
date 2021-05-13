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
#include "host/libs/screen_connector/screen_connector.h"

namespace cuttlefish {
namespace confui {

/**
 * create a raw frame for confirmation UI dialog
 */
class ConfUiRenderer {
 public:
  using LabelConfMsg = teeui::LabelBody;

  ConfUiRenderer(const std::uint32_t display);

  /**
   * this does not repaint from the scratch all the time
   *
   * Unless repainting the whole thing is needed, it remove the message
   * label, and re-draw there. There seems yet no fancy way of doing this.
   * Thus, it repaint the background color on the top of the label, and
   * draw the label on the new background
   *
   * As HostRenderer is intended to be shared across sessions, HostRender
   * owns the buffer, and returns reference to the buffer. Note that no
   * 2 or more sessions are concurrently executed. Only 1 or 0 is active
   * at the given moment.
   */
  std::tuple<TeeUiFrame&, bool> RenderRawFrame(
      const std::string& confirmation_msg, const std::string& lang_id = "en");

  bool IsFrameReady() const { return !raw_frame_.empty(); }

 private:
  struct Boundary {            // inclusive but.. LayoutElement's size is float
    std::uint32_t x, y, w, h;  // (x, y) is the top left
  };

  template <typename LayoutElement>
  Boundary GetBoundary(LayoutElement&& e) {
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

  // essentially, to repaint from the scratch, so returns new frame
  // when successful. Or, nullopt
  std::optional<TeeUiFrame> RepaintRawFrame(const std::string& confirmation_msg,
                                            const std::string& lang_id = "en");

  bool InitLayout(const std::string& lang_id);
  teeui::Error UpdateTranslations();
  /**
   * could be confusing. update prompt_, and update the text_ in the Label
   * object, the GUI components. This does not render immediately. And..
   * to render it, we must clean up the existing dirty pixels, which
   * this method does not do.
   */
  void SetConfUiMessage(const std::string& s);
  teeui::Error SetLangId(const std::string& lang_id);
  teeui::context<teeui::ConUIParameters> GetDeviceContext();

  // effectively, will be send to teeui as a callback function
  teeui::Error UpdatePixels(TeeUiFrame& buffer, std::uint32_t x,
                            std::uint32_t y, teeui::Color color);

  // from Trusty
  // second param is for type deduction
  template <typename... Elements>
  static teeui::Error drawElements(std::tuple<Elements...>& layout,
                                   const teeui::PixelDrawer& drawPixel) {
    // Error::operator|| is overloaded, so we don't get short circuit
    // evaluation. But we get the first error that occurs. We will still try and
    // draw the remaining elements in the order they appear in the layout tuple.
    return (std::get<Elements>(layout).draw(drawPixel) || ...);
  }

  // repaint the confirmation UI label only
  teeui::Error RenderConfirmationMsgOnly(const std::string& confirmation_msg);

  // from Trusty
  template <typename Context>
  void UpdateColorScheme(Context* ctx) {
    using namespace teeui;
    color_text_ = is_inverted_ ? kColorDisabledInv : kColorDisabled;
    shield_color_ = is_inverted_ ? kColorShieldInv : kColorShield;
    color_bg_ = is_inverted_ ? kColorBackgroundInv : kColorBackground;

    ctx->template setParam<ShieldColor>(shield_color_);
    ctx->template setParam<ColorText>(color_text_);
    ctx->template setParam<ColorBG>(color_bg_);
    return;
  }

  template <typename Label>
  auto SetText(const std::string& text) {
    return std::get<Label>(layout_).setText(
        {text.c_str(), text.c_str() + text.size()});
  }

  /**
   * source:
   * https://android.googlesource.com/trusty/app/confirmationui/+/0429cc7/src/trusty_confirmation_ui.cpp#49
   */
  template <typename Label>
  teeui::Error UpdateString() {
    using namespace teeui;
    const char* str;
    auto& label = std::get<Label>(layout_);
    str = localization::lookup(TranslationId(label.textId()));
    if (str == nullptr) {
      ConfUiLog(ERROR) << "Given translation_id" << label.textId()
                       << "not found";
      return Error::Localization;
    }
    label.setText({str, str + strlen(str)});
    return Error::OK;
  }

  const int display_num_;
  teeui::layout_t<teeui::ConfUILayout> layout_;
  std::string lang_id_;
  std::string prompt_;  // confirmation ui message
  TeeUiFrame raw_frame_;
  std::uint32_t current_height_;
  std::uint32_t current_width_;
  teeui::Color color_bg_;
  teeui::Color color_text_;
  teeui::Color shield_color_;
  bool is_inverted_;
  teeui::context<teeui::ConUIParameters> ctx_;

  static constexpr const teeui::Color kColorEnabled = 0xff212121;
  static constexpr const teeui::Color kColorDisabled = 0xffbdbdbd;
  static constexpr const teeui::Color kColorEnabledInv = 0xffdedede;
  static constexpr const teeui::Color kColorDisabledInv = 0xff424242;
  static constexpr const teeui::Color kColorBackground = 0xffffffff;
  static constexpr const teeui::Color kColorBackgroundInv = 0xff212121;
  static constexpr const teeui::Color kColorShieldInv = 0xffc4cb80;
  static constexpr const teeui::Color kColorShield = 0xff778500;
};
}  // end of namespace confui
}  // end of namespace cuttlefish
