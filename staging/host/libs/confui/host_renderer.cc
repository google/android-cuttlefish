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

#include "host/libs/confui/host_renderer.h"

#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace confui {
static teeui::Color alfaCombineChannel(std::uint32_t shift, double alfa,
                                       teeui::Color a, teeui::Color b) {
  a >>= shift;
  a &= 0xff;
  b >>= shift;
  b &= 0xff;
  double acc = alfa * a + (1 - alfa) * b;
  if (acc <= 0) return 0;
  std::uint32_t result = acc;
  if (result > 255) return 255 << shift;
  return result << shift;
}

std::unique_ptr<ConfUiRenderer> ConfUiRenderer::GenerateRenderer(
    const std::uint32_t display, const std::string& confirmation_msg,
    const std::string& locale, const bool inverted, const bool magnified) {
  ConfUiRenderer* raw_ptr = new ConfUiRenderer(display, confirmation_msg,
                                               locale, inverted, magnified);
  if (raw_ptr && raw_ptr->IsSetUpSuccessful()) {
    return std::unique_ptr<ConfUiRenderer>(raw_ptr);
  }
  return nullptr;
}

static int GetDpi(const int display_num = 0) {
  auto config = CuttlefishConfig::Get();
  CHECK(config) << "Config is Missing";
  auto instance = config->ForDefaultInstance();
  auto display_configs = instance.display_configs();
  CHECK_GT(display_configs.size(), display_num)
      << "Invalid display number " << display_num;
  return display_configs[display_num].dpi;
}

/**
 * device configuration
 *
 * ctx_{# of pixels in 1 mm, # of pixels per 1 density independent pixels}
 *
 * The numbers are, however, to fit for the host webRTC local/remote clients
 * in general, not necessarily the supposedly guest device (e.g. Auto, phone,
 * etc)
 *
 * In general, for a normal PC, roughly ctx_(6.45211, 400.0/412.0) is a good
 * combination for the default DPI, 320. If we want to see the impact
 * of the change in the guest DPI, we could adjust the combination above
 * proportionally
 *
 */
ConfUiRenderer::ConfUiRenderer(const std::uint32_t display,
                               const std::string& confirmation_msg,
                               const std::string& locale, const bool inverted,
                               const bool magnified)
    : display_num_{display},
      lang_id_{locale},
      prompt_text_{confirmation_msg},
      current_height_{ScreenConnectorInfo::ScreenHeight(display_num_)},
      current_width_{ScreenConnectorInfo::ScreenWidth(display_num_)},
      is_inverted_(inverted),
      is_magnified_(magnified),
      ctx_(6.45211 * GetDpi() / 320.0, 400.0 / 412.0 * GetDpi() / 320.0),
      is_setup_well_(false) {
  SetDeviceContext(current_width_, current_height_, is_inverted_,
                   is_magnified_);
  layout_ = teeui::instantiateLayout(teeui::ConfUILayout(), ctx_);

  if (auto error = UpdateLocale()) {
    ConfUiLog(ERROR) << "Update Translation Error: " << Enum2Base(error.code());
    // is_setup_well_ = false;
    return;
  }
  UpdateColorScheme(is_inverted_);
  SetText<LabelConfMsg>(prompt_text_);
  is_setup_well_ = true;
}

teeui::Error ConfUiRenderer::UpdateLocale() {
  using teeui::Error;
  teeui::localization::selectLangId(lang_id_.c_str());
  if (auto error = UpdateTranslations()) {
    return error;
  }
  return Error::OK;
}

template <typename Label>
teeui::Error ConfUiRenderer::UpdateString() {
  using namespace teeui;
  const char* str;
  auto& label = std::get<Label>(layout_);
  str = localization::lookup(TranslationId(label.textId()));
  if (str == nullptr) {
    ConfUiLog(ERROR) << "Given translation_id" << label.textId() << "not found";
    return Error::Localization;
  }
  label.setText({str, str + strlen(str)});
  return Error::OK;
}

teeui::Error ConfUiRenderer::UpdateTranslations() {
  using namespace teeui;
  if (auto error = UpdateString<LabelOK>()) {
    return error;
  }
  if (auto error = UpdateString<LabelCancel>()) {
    return error;
  }
  if (auto error = UpdateString<LabelTitle>()) {
    return error;
  }
  if (auto error = UpdateString<LabelHint>()) {
    return error;
  }
  return Error::OK;
}

void ConfUiRenderer::SetDeviceContext(const unsigned long long w,
                                      const unsigned long long h,
                                      const bool is_inverted,
                                      const bool is_magnified) {
  using namespace teeui;
  const auto screen_width = operator""_px(w);
  const auto screen_height = operator""_px(h);
  ctx_.setParam<RightEdgeOfScreen>(pxs(screen_width));
  ctx_.setParam<BottomOfScreen>(pxs(screen_height));
  if (is_magnified) {
    ctx_.setParam<DefaultFontSize>(18_dp);
    ctx_.setParam<BodyFontSize>(20_dp);
  } else {
    ctx_.setParam<DefaultFontSize>(14_dp);
    ctx_.setParam<BodyFontSize>(16_dp);
  }
  if (is_inverted) {
    ctx_.setParam<ShieldColor>(kColorShieldInv);
    ctx_.setParam<ColorText>(kColorTextInv);
    ctx_.setParam<ColorBG>(kColorBackgroundInv);
    ctx_.setParam<ColorButton>(kColorShieldInv);
  } else {
    ctx_.setParam<ShieldColor>(kColorShield);
    ctx_.setParam<ColorText>(kColorText);
    ctx_.setParam<ColorBG>(kColorBackground);
    ctx_.setParam<ColorButton>(kColorShield);
  }
}

teeui::Error ConfUiRenderer::UpdatePixels(TeeUiFrameWrapper& raw_frame,
                                          std::uint32_t x, std::uint32_t y,
                                          teeui::Color color) {
  auto buffer = raw_frame.data();
  const auto height = raw_frame.Height();
  const auto width = raw_frame.Width();
  auto pos = width * y + x;
  if (pos >= (height * width)) {
    ConfUiLog(ERROR) << "Rendering Out of Bound";
    return teeui::Error::OutOfBoundsDrawing;
  }
  const double alfa = ((color & 0xff000000) >> 24) / 255.0;
  auto& pixel = *reinterpret_cast<teeui::Color*>(buffer + pos);
  pixel = alfaCombineChannel(0, alfa, color, pixel) |
          alfaCombineChannel(8, alfa, color, pixel) |
          alfaCombineChannel(16, alfa, color, pixel);
  return teeui::Error::OK;
}

void ConfUiRenderer::UpdateColorScheme(const bool is_inverted) {
  using namespace teeui;
  color_text_ = is_inverted ? kColorDisabledInv : kColorDisabled;
  shield_color_ = is_inverted ? kColorShieldInv : kColorShield;
  color_bg_ = is_inverted ? kColorBackgroundInv : kColorBackground;

  ctx_.setParam<ShieldColor>(shield_color_);
  ctx_.setParam<ColorText>(color_text_);
  ctx_.setParam<ColorBG>(color_bg_);
  return;
}

std::shared_ptr<TeeUiFrameWrapper> ConfUiRenderer::RenderRawFrame() {
  /* we repaint only if one or more of the followng meet:
   *
   *  1. raw_frame_ is empty
   *  2. the current_width_ and current_height_ is out of date
   *
   */
  const int w = ScreenConnectorInfo::ScreenWidth(display_num_);
  const int h = ScreenConnectorInfo::ScreenHeight(display_num_);
  if (!IsFrameReady() || current_height_ != h || current_width_ != w) {
    auto new_frame = RepaintRawFrame(w, h);
    if (!new_frame) {
      // must repaint but failed
      raw_frame_ = nullptr;
      return nullptr;
    }
    // repainting from the scratch successful in a new frame
    raw_frame_ = std::move(new_frame);
    current_width_ = w;
    current_height_ = h;
  }
  return raw_frame_;
}

std::unique_ptr<TeeUiFrameWrapper> ConfUiRenderer::RepaintRawFrame(
    const int w, const int h) {
  std::get<teeui::LabelOK>(layout_).setTextColor(kColorEnabled);
  std::get<teeui::LabelCancel>(layout_).setTextColor(kColorEnabled);

  /**
   * should be uint32_t for teeui APIs.
   * It assumes that each raw frame buffer element is 4 bytes
   */
  const teeui::Color background_color =
      is_inverted_ ? kColorBackgroundInv : kColorBackground;
  auto new_raw_frame =
      std::make_unique<TeeUiFrameWrapper>(w, h, background_color);
  auto draw_pixel = teeui::makePixelDrawer(
      [this, &new_raw_frame](std::uint32_t x, std::uint32_t y,
                             teeui::Color color) -> teeui::Error {
        return this->UpdatePixels(*new_raw_frame, x, y, color);
      });

  // render all components
  const auto error = drawElements(layout_, draw_pixel);
  if (error) {
    ConfUiLog(ERROR) << "Painting failed: " << error.code();
    return nullptr;
  }

  return new_raw_frame;
}

}  // end of namespace confui
}  // end of namespace cuttlefish
