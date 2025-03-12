/*
 *
 * Copyright 2019, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LIBTEEUI_LABEL_H_
#define LIBTEEUI_LABEL_H_

#include "utf8range.h"
#include "utils.h"

// #define DRAW_DEBUG_MARKERS

namespace teeui {

enum class Alignment : int8_t { LEFT, CENTER, RIGHT, TOP, BOTTOM };

class FontBuffer {
    const uint8_t* data_;
    size_t size_;

  public:
    constexpr FontBuffer() : data_(nullptr), size_(0) {}
    constexpr FontBuffer(const uint8_t* data, size_t size) noexcept : data_(data), size_(size) {}
    template <size_t size>
    explicit constexpr FontBuffer(const uint8_t (&data)[size]) noexcept
        : data_(&data[0]), size_(size) {}
    constexpr FontBuffer(const FontBuffer&) noexcept = default;
    constexpr FontBuffer(FontBuffer&&) noexcept = default;
    FontBuffer& operator=(FontBuffer&&) noexcept = default;
    FontBuffer& operator=(const FontBuffer&) noexcept = default;

    constexpr operator bool() const { return data_ != nullptr; }

    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }
};

class LabelImpl {
    using text_t = UTF8Range<const char*>;

  public:
    struct LineInfo {
        struct info_t {
            Point<pxs> lineStart;
            text_t lineText;
        };
        size_t size_;
        info_t* info_;
        info_t* begin() { return &info_[0]; }
        info_t* end() { return &info_[size_]; }
        const info_t* begin() const { return &info_[0]; }
        const info_t* end() const { return &info_[size_]; }
    };

    LabelImpl()
        : fontSize_(10_px), lineHeight_(12_px), text_{}, horizontalTextAlignment_(Alignment::LEFT),
          verticalTextAlignment_(Alignment::TOP), textColor_(0), font_{}, textId_(0) {}
    LabelImpl(pxs fontSize, pxs lineHeight, text_t text, Alignment horizontal,
              Alignment verticalJustified, Color textColor, FontBuffer font, uint64_t textId)
        : fontSize_(fontSize), lineHeight_(lineHeight), text_(text),
          horizontalTextAlignment_(horizontal), verticalTextAlignment_(verticalJustified),
          textColor_(textColor), font_(font), textId_(textId) {}

    pxs fontSize() const { return fontSize_; }

    void setText(text_t text) { text_ = text; }
    void setTextColor(Color color) { textColor_ = color; }

    text_t text() const { return text_; }
    uint64_t textId() const { return textId_; }

    Error draw(const PixelDrawer& drawPixel, const Box<pxs>& bounds, LineInfo* lineInfo);
    void setCB(CallbackEvent cbEvent) { cbEvent_ = std::move(cbEvent); }
    optional<CallbackEvent> getCB() { return cbEvent_; }
    Error hit(const Event& event, const Box<pxs>& bounds);

  private:
    pxs fontSize_;
    pxs lineHeight_;
    text_t text_;
    Alignment horizontalTextAlignment_;
    Alignment verticalTextAlignment_;
    Color textColor_;
    FontBuffer font_;
    uint64_t textId_;
    optional<CallbackEvent> cbEvent_;
};

/**
 * Label is a LayoutElement and should be used as second argument in the BEGIN_ELEMENT() macro.
 * The template argument Derived is the new class derived from Label, that is created by the
 * BEGIN_ELEMENT() macro.
 */
template <typename Derived> class Label : public LayoutElement<Derived>, public LabelImpl {
  public:
    static const constexpr Alignment label_horizontal_text_alignment = Alignment::LEFT;
    static const constexpr Alignment label_vertical_text_alignment = Alignment::TOP;
    static const constexpr Color label_text_color = 0xff000000;
    static const constexpr int label_font = 0;
    static const constexpr uint64_t text_id = 0;

    Label() = default;
    template <typename Context>
    Label(const Context& context)
        : LayoutElement<Derived>(context),
          LabelImpl(
              context = Derived::label_font_size, context = Derived::label_line_height,
              {&Derived::label_text[0], &Derived::label_text[sizeof(Derived::label_text) - 1]},
              Derived::label_horizontal_text_alignment, Derived::label_vertical_text_alignment,
              context = Derived::label_text_color, getFont(Derived::label_font), Derived::text_id) {
    }

    Error draw(const PixelDrawer& drawPixel) {
        LabelImpl::LineInfo::info_t lines[Derived::label_number_of_lines];
        LabelImpl::LineInfo lineInfo = {Derived::label_number_of_lines, lines};
        return LabelImpl::draw(drawPixel, this->bounds_, &lineInfo);
    }

    Error hit(const Event& event) { return LabelImpl::hit(event, this->bounds_); }
};

}  //  namespace teeui

#define FontSize(fs) static const constexpr auto label_font_size = fs

#define DefaultText(text) static const constexpr char label_text[] = text

#define LineHeight(height) static const constexpr auto label_line_height = height

#define NumberOfLines(lines) static const constexpr auto label_number_of_lines = lines

#define HeightFromLines (label_line_height * pxs(label_number_of_lines))

#define HorizontalTextAlignment(horizontalAligment)                                                \
    static const constexpr Alignment label_horizontal_text_alignment = horizontalAligment;

#define LeftJustified HorizontalTextAlignment(Alignment::LEFT)
#define CenterJustified HorizontalTextAlignment(Alignment::CENTER)
#define RightJustified HorizontalTextAlignment(Alignment::RIGHT)

#define VerticalTextAlignment(verticalAligment)                                                    \
    static const constexpr Alignment label_vertical_text_alignment = verticalAligment;

#define VerticallyTop VerticalTextAlignment(Alignment::TOP)
#define VerticallyCentered VerticalTextAlignment(Alignment::CENTER)

#define TextColor(color) static const constexpr auto label_text_color = color

#define FONT(name) TEEUI_FONT_##name()

#define DECLARE_FONT_BUFFER(name, buffer, ...)                                                     \
    struct TEEUI_FONT_##name {};                                                                   \
    inline FontBuffer getFont(TEEUI_FONT_##name) { return FontBuffer(buffer, ##__VA_ARGS__); }

#define Font(fontbuffer) static const constexpr auto label_font = fontbuffer

#define TextID(tid) static const constexpr uint64_t text_id = tid

#endif  // LIBTEEUI_LABEL_H_
