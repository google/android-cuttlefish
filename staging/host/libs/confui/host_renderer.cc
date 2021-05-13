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

ConfUiRenderer::ConfUiRenderer(const std::uint32_t display)
    : display_num_(display),
      lang_id_{"en"},
      prompt_("Am I Yumi Meow?"),
      current_height_(ScreenConnectorInfo::ScreenHeight(display_num_)),
      current_width_(ScreenConnectorInfo::ScreenWidth(display_num_)),
      color_bg_{kColorBackground},
      color_text_{kColorDisabled},
      shield_color_{kColorShield},
      is_inverted_{false},
      ctx_{GetDeviceContext()} {
  auto opted_frame = RepaintRawFrame(prompt_, lang_id_);
  if (opted_frame) {
    raw_frame_ = std::move(opted_frame.value());
  }
}

void ConfUiRenderer::SetConfUiMessage(const std::string& msg) {
  prompt_ = msg;
  SetText<LabelConfMsg>(msg);
}

teeui::Error ConfUiRenderer::SetLangId(const std::string& lang_id) {
  using teeui::Error;
  lang_id_ = lang_id;
  teeui::localization::selectLangId(lang_id_.c_str());
  if (auto error = UpdateTranslations()) {
    return error;
  }
  return Error::OK;
}

teeui::Error ConfUiRenderer::UpdateTranslations() {
  using namespace teeui;
  if (auto error = UpdateString<LabelOK>()) {
    return error;
  }
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

teeui::context<teeui::ConUIParameters> ConfUiRenderer::GetDeviceContext() {
  using namespace teeui;
  const unsigned long long w = ScreenConnectorInfo::ScreenWidth(display_num_);
  const unsigned long long h = ScreenConnectorInfo::ScreenHeight(display_num_);
  const auto screen_width = operator""_px(w);
  const auto screen_height = operator""_px(h);
  context<teeui::ConUIParameters> ctx(6.45211, 400.0 / 412.0);
  ctx.setParam<RightEdgeOfScreen>(screen_width);
  ctx.setParam<BottomOfScreen>(screen_height);
  ctx.setParam<PowerButtonTop>(20.26_mm);
  ctx.setParam<PowerButtonBottom>(30.26_mm);
  ctx.setParam<VolUpButtonTop>(40.26_mm);
  ctx.setParam<VolUpButtonBottom>(50.26_mm);
  ctx.setParam<DefaultFontSize>(14_dp);
  ctx.setParam<BodyFontSize>(16_dp);
  return ctx;
}

bool ConfUiRenderer::InitLayout(const std::string& lang_id) {
  layout_ = teeui::instantiateLayout(teeui::ConfUILayout(), ctx_);
  SetLangId(lang_id);
  if (auto error = UpdateTranslations()) {
    ConfUiLog(ERROR) << "Update Translation Error";
    return false;
  }
  UpdateColorScheme(&ctx_);
  return true;
}

teeui::Error ConfUiRenderer::UpdatePixels(TeeUiFrame& raw_frame,
                                          std::uint32_t x, std::uint32_t y,
                                          teeui::Color color) {
  auto buffer = raw_frame.data();
  const auto height = ScreenConnectorInfo::ScreenHeight(display_num_);
  const auto width = ScreenConnectorInfo::ScreenWidth(display_num_);
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

std::tuple<TeeUiFrame&, bool> ConfUiRenderer::RenderRawFrame(
    const std::string& confirmation_msg, const std::string& lang_id) {
  /* we repaint only if one or more of the followng meet:
   *
   *  1. raw_frame_ is empty
   *  2. the current_width_ and current_height_ is out of date
   *  3. lang_id is different (e.g. new locale)
   *
   *  in the future, maybe you wanna inverted, new background, etc?
   */
  if (lang_id != lang_id_ || !IsFrameReady() ||
      current_height_ != ScreenConnectorInfo::ScreenHeight(display_num_) ||
      current_width_ != ScreenConnectorInfo::ScreenWidth(display_num_)) {
    auto opted_new_frame = RepaintRawFrame(confirmation_msg, lang_id_);
    if (opted_new_frame) {
      // repainting from the scratch successful in a new frame
      raw_frame_ = std::move(opted_new_frame.value());
      return {raw_frame_, true};
    }
    // repaint failed even if it was necessary, so returns invalid values
    raw_frame_.clear();
    return {raw_frame_, false};
  }
  // no need to repaint from the scratch. repaint the confirmation message only
  // the frame is mostly already in raw_frame_
  auto ret_code = RenderConfirmationMsgOnly(confirmation_msg);
  return {raw_frame_, (ret_code == teeui::Error::OK)};
}

std::optional<TeeUiFrame> ConfUiRenderer::RepaintRawFrame(
    const std::string& confirmation_msg, const std::string& lang_id) {
  /*
   * NOTE: DON'T use current_width_/height_ to create this frame
   * it may fail, and then we must not mess up the current_width_, height_
   *
   */
  if (!InitLayout(lang_id)) {
    return std::nullopt;
  }
  SetConfUiMessage(confirmation_msg);
  auto color = kColorEnabled;
  std::get<teeui::LabelOK>(layout_).setTextColor(color);
  std::get<teeui::LabelCancel>(layout_).setTextColor(color);

  /* in the future, if ever we need to register a handler for the
     Label{OK,Cancel}. do this: std::get<teeui::LabelOK>(layout_)
     .setCB(teeui::makeCallback<teeui::Error, teeui::Event>(
     [](teeui::Event e, void* p) -> teeui::Error {
     LOG(DEBUG) << "Calling callback for Confirm?";
     return reinterpret_cast<decltype(owner)*>(p)->TapOk(e); },
     owner));
  */
  // we manually check if click happened, where if yes, and generate the label
  // event manually. So we won't register the handler here.
  /**
   * should be uint32_t for teeui APIs.
   * It assumes that each raw frame buffer element is 4 bytes
   */
  TeeUiFrame new_raw_frame(
      ScreenConnectorInfo::ScreenSizeInBytes(display_num_) / 4,
      kColorBackground);

  auto draw_pixel = teeui::makePixelDrawer(
      [this, &new_raw_frame](std::uint32_t x, std::uint32_t y,
                             teeui::Color color) -> teeui::Error {
        return this->UpdatePixels(new_raw_frame, x, y, color);
      });

  // render all components
  const auto error = drawElements(layout_, draw_pixel);
  if (error) {
    ConfUiLog(ERROR) << "Painting failed: " << error.code();
    return std::nullopt;
  }

  // set current frame's dimension as frame generation was successful
  current_height_ = ScreenConnectorInfo::ScreenHeight(display_num_);
  current_width_ = ScreenConnectorInfo::ScreenWidth(display_num_);

  return {new_raw_frame};
}

teeui::Error ConfUiRenderer::RenderConfirmationMsgOnly(
    const std::string& confirmation_msg) {
  // repaint labelbody on the raw_frame__ only
  auto callback_func = [this](std::uint32_t x, std::uint32_t y,
                              teeui::Color color) -> teeui::Error {
    return UpdatePixels(raw_frame_, x, y, color);
  };
  auto draw_pixel = teeui::makePixelDrawer(callback_func);
  LabelConfMsg& label = std::get<LabelConfMsg>(layout_);
  auto b = GetBoundary(label);
  for (std::uint32_t i = 0; i != b.w; i++) {
    const auto col_index = i + b.x - 1;
    for (std::uint32_t j = 0; j != b.y; j++) {
      const auto row_index = (j + b.y - 1);
      raw_frame_[current_width_ * row_index + col_index] = color_bg_;
    }
  }

  SetConfUiMessage(confirmation_msg);
  ConfUiLog(DEBUG) << "Repaint Confirmation Msg with :" << prompt_;
  if (auto error = std::get<LabelConfMsg>(layout_).draw(draw_pixel)) {
    ConfUiLog(ERROR) << "Repainting Confirmation Message Label failed:"
                     << error.code();
    return error;
  }
  return teeui::Error::OK;
}

}  // end of namespace confui
}  // end of namespace cuttlefish
