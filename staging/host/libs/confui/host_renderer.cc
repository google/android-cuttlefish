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

/**
 * create a raw frame for confirmation UI dialog
 *
 * Many rendering code borrowed from the following source
 *  https://android.googlesource.com/trusty/app/confirmationui/+/0429cc7/src
 */
class ConfUiRendererImpl {
  friend class ConfUiRenderer;

 public:
  using LabelConfMsg = teeui::LabelBody;

 private:
  static Result<std::unique_ptr<ConfUiRendererImpl>> GenerateRenderer(
      const std::uint32_t display, const std::string& confirmation_msg,
      const std::string& locale, const bool inverted, const bool magnified);

  /**
   * this does not repaint from the scratch all the time
   *
   * It does repaint its frame buffer only when w/h of
   * current display has changed
   */
  std::unique_ptr<TeeUiFrameWrapper>& RenderRawFrame();

  bool IsFrameReady() const { return raw_frame_ && !raw_frame_->IsEmpty(); }

  bool IsInConfirm(const std::uint32_t x, const std::uint32_t y) {
    return IsInside<teeui::LabelOK>(x, y);
  }
  bool IsInCancel(const std::uint32_t x, const std::uint32_t y) {
    return IsInside<teeui::LabelCancel>(x, y);
  }

  bool IsSetUpSuccessful() const { return is_setup_well_; }
  ConfUiRendererImpl(const std::uint32_t display,
                     const std::string& confirmation_msg,
                     const std::string& locale, const bool inverted,
                     const bool magnified);

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
  std::unique_ptr<TeeUiFrameWrapper> raw_frame_;
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

Result<std::unique_ptr<ConfUiRendererImpl>>
ConfUiRendererImpl::GenerateRenderer(const std::uint32_t display,
                                     const std::string& confirmation_msg,
                                     const std::string& locale,
                                     const bool inverted,
                                     const bool magnified) {
  ConfUiRendererImpl* raw_ptr = new ConfUiRendererImpl(
      display, confirmation_msg, locale, inverted, magnified);
  CF_EXPECT(raw_ptr && raw_ptr->IsSetUpSuccessful(),
            "Failed to create ConfUiRendererImpl");
  return std::unique_ptr<ConfUiRendererImpl>(raw_ptr);
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
ConfUiRendererImpl::ConfUiRendererImpl(const std::uint32_t display,
                                       const std::string& confirmation_msg,
                                       const std::string& locale,
                                       const bool inverted,
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

teeui::Error ConfUiRendererImpl::UpdateLocale() {
  using teeui::Error;
  teeui::localization::selectLangId(lang_id_.c_str());
  if (auto error = UpdateTranslations()) {
    return error;
  }
  return Error::OK;
}

template <typename Label>
teeui::Error ConfUiRendererImpl::UpdateString() {
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

teeui::Error ConfUiRendererImpl::UpdateTranslations() {
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

void ConfUiRendererImpl::SetDeviceContext(const unsigned long long w,
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

teeui::Error ConfUiRendererImpl::UpdatePixels(TeeUiFrameWrapper& raw_frame,
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

void ConfUiRendererImpl::UpdateColorScheme(const bool is_inverted) {
  using namespace teeui;
  color_text_ = is_inverted ? kColorDisabledInv : kColorDisabled;
  shield_color_ = is_inverted ? kColorShieldInv : kColorShield;
  color_bg_ = is_inverted ? kColorBackgroundInv : kColorBackground;

  ctx_.setParam<ShieldColor>(shield_color_);
  ctx_.setParam<ColorText>(color_text_);
  ctx_.setParam<ColorBG>(color_bg_);
  return;
}

std::unique_ptr<TeeUiFrameWrapper>& ConfUiRendererImpl::RenderRawFrame() {
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
      return raw_frame_;
    }
    // repainting from the scratch successful in a new frame
    raw_frame_ = std::move(new_frame);
    current_width_ = w;
    current_height_ = h;
  }
  return raw_frame_;
}

std::unique_ptr<TeeUiFrameWrapper> ConfUiRendererImpl::RepaintRawFrame(
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

ConfUiRenderer::ConfUiRenderer(ScreenConnectorFrameRenderer& screen_connector)
    : screen_connector_{screen_connector} {}

ConfUiRenderer::~ConfUiRenderer() {}

Result<void> ConfUiRenderer::RenderDialog(
    const std::uint32_t display_num, const std::string& prompt_text,
    const std::string& locale, const std::vector<teeui::UIOption>& ui_options) {
  renderer_impl_ = CF_EXPECT(ConfUiRendererImpl::GenerateRenderer(
      display_num, prompt_text, locale, IsInverted(ui_options),
      IsMagnified(ui_options)));
  auto& teeui_frame = renderer_impl_->RenderRawFrame();
  CF_EXPECT(teeui_frame != nullptr, "RenderRawFrame() failed.");
  ConfUiLog(VERBOSE) << "actually trying to render the frame"
                     << thread::GetName();
  auto frame_width = teeui_frame->Width();
  auto frame_height = teeui_frame->Height();
  auto frame_stride_bytes = teeui_frame->ScreenStrideBytes();
  auto frame_bytes = reinterpret_cast<std::uint8_t*>(teeui_frame->data());
  CF_EXPECT(screen_connector_.RenderConfirmationUi(
      display_num, frame_width, frame_height, frame_stride_bytes, frame_bytes));
  return {};
}

bool ConfUiRenderer::IsInverted(
    const std::vector<teeui::UIOption>& ui_options) const {
  return Contains(ui_options, teeui::UIOption::AccessibilityInverted);
}

bool ConfUiRenderer::IsMagnified(
    const std::vector<teeui::UIOption>& ui_options) const {
  return Contains(ui_options, teeui::UIOption::AccessibilityMagnified);
}

bool ConfUiRenderer::IsInConfirm(const std::uint32_t x, const std::uint32_t y) {
  if (!renderer_impl_) {
    ConfUiLog(INFO) << "renderer_impl_ is nullptr";
  }
  return renderer_impl_ && renderer_impl_->IsInConfirm(x, y);
}
bool ConfUiRenderer::IsInCancel(const std::uint32_t x, const std::uint32_t y) {
  if (!renderer_impl_) {
    ConfUiLog(INFO) << "renderer_impl_ is nullptr";
  }
  return renderer_impl_ && renderer_impl_->IsInCancel(x, y);
}

}  // end of namespace confui
}  // end of namespace cuttlefish
