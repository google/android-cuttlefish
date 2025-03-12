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

#ifndef TEEUI_LIBTEEUI_FONT_RENDERING_H_
#define TEEUI_LIBTEEUI_FONT_RENDERING_H_

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/ftglyph.h>

#include "utils.h"
#include <tuple>

#include <type_traits>

#include "utf8range.h"

namespace teeui {

template <typename T> struct HandleDelete;

template <typename T, typename Deleter = HandleDelete<T>> class Handle {
  public:
    Handle() : handle_(nullptr) {}
    explicit Handle(T handle) : handle_(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) {
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }

    Handle& operator=(Handle&& rhs) {
        if (&rhs != this) {
            auto dummy = handle_;
            handle_ = rhs.handle_;
            rhs.handle_ = dummy;
        }
        return *this;
    }

    ~Handle() {
        if (handle_) Deleter()(handle_);
    }

    operator bool() const { return handle_ != nullptr; }
    T operator*() const { return handle_; }
    const T operator->() const { return handle_; }
    T operator->() { return handle_; }

  private:
    T handle_;
};

#define MAP_HANDLE_DELETER(type, deleter)                                                          \
    template <> struct HandleDelete<type> {                                                        \
        void operator()(type h) { deleter(h); }                                                    \
    }

MAP_HANDLE_DELETER(FT_Face, FT_Done_Face);
MAP_HANDLE_DELETER(FT_Library, FT_Done_FreeType);


bool isBreakable(unsigned long codePoint);
bool isNewline(unsigned long codePoint);

template <typename CharIterator> class UTF8WordRange {
    UTF8Range<CharIterator> range_;

  public:
    UTF8WordRange(CharIterator begin, CharIterator end) : range_(begin, end) {}
    explicit UTF8WordRange(const UTF8Range<CharIterator>& range) : range_(range) {}
    UTF8WordRange() = default;
    UTF8WordRange(const UTF8WordRange&) = default;
    UTF8WordRange(UTF8WordRange&&) = default;
    UTF8WordRange& operator=(UTF8WordRange&&) = default;
    UTF8WordRange& operator=(const UTF8WordRange&) = default;

    using UTF8Iterator = typename UTF8Range<CharIterator>::Iter;
    class Iter {
        UTF8Iterator begin_;
        UTF8Iterator end_;

      public:
        Iter() : begin_{} {}
        Iter(UTF8Iterator begin, UTF8Iterator end) : begin_(begin), end_(end) {}
        Iter(const Iter& rhs) : begin_(rhs.begin_), end_(rhs.end_) {}
        Iter& operator=(const Iter& rhs) {
            begin_ = rhs.begin_;
            end_ = rhs.end_;
            return *this;
        }
        UTF8Iterator operator*() const { return begin_; }
        Iter& operator++() {
            if (begin_ == end_) return *this;
            bool prevBreaking = isBreakable(begin_.codePoint());
            // checkAndUpdate detects edges between breakable and non breakable characters.
            // As a result the iterator stops on the first character of a word or whitespace
            // sequence.
            auto checkAndUpdate = [&](unsigned long cp) {
                bool current = isBreakable(cp);
                bool result = prevBreaking == current;
                prevBreaking = current;
                return result;
            };
            do {
                ++begin_;
            } while (begin_ != end_ && checkAndUpdate(begin_.codePoint()));
            return *this;
        }
        Iter operator++(int) {
            Iter dummy = *this;
            ++(*this);
            return dummy;
        }
        bool operator==(const Iter& rhs) const { return begin_ == rhs.begin_; }
        bool operator!=(const Iter& rhs) const { return !(*this == rhs); }
    };
    Iter begin() const { return Iter(range_.begin(), range_.end()); }
    Iter end() const { return Iter(range_.end(), range_.end()); }
};

class TextContext;

using GlyphIndex = unsigned int;

class TextFace {
    friend TextContext;
    Handle<FT_Face> face_;
    bool hasKerning_ = false;

  public:
    Error setCharSize(signed long char_size, unsigned int dpi);
    Error setCharSizeInPix(pxs size);
    GlyphIndex getCharIndex(unsigned long codePoint);
    Error loadGlyph(GlyphIndex index);
    Error renderGlyph();

    Error drawGlyph(const Vec2d<pxs>& pos, const PixelDrawer& drawPixel) {
        FT_Bitmap* bitmap = &face_->glyph->bitmap;
        uint8_t* rowBuffer = bitmap->buffer;
        Vec2d<pxs> offset{face_->glyph->bitmap_left, -face_->glyph->bitmap_top};
        auto bPos = pos + offset;
        for (unsigned y = 0; y < bitmap->rows; ++y) {
            for (unsigned x = 0; x < bitmap->width; ++x) {
                Color alpha = 0;
                switch (bitmap->pixel_mode) {
                case FT_PIXEL_MODE_GRAY:
                    alpha = rowBuffer[x];
                    alpha *= 256;
                    alpha /= bitmap->num_grays;
                    alpha <<= 24;
                    break;
                case FT_PIXEL_MODE_LCD:
                case FT_PIXEL_MODE_BGRA:
                case FT_PIXEL_MODE_NONE:
                case FT_PIXEL_MODE_LCD_V:
                case FT_PIXEL_MODE_MONO:
                case FT_PIXEL_MODE_GRAY2:
                case FT_PIXEL_MODE_GRAY4:
                default:
                    return Error::UnsupportedPixelFormat;
                }
                if (drawPixel(bPos.x().count() + x, bPos.y().count() + y, alpha)) {
                    return Error::OutOfBoundsDrawing;
                }
            }
            rowBuffer += bitmap->pitch;
        }
        return Error::OK;
    }

    Vec2d<pxs> advance() const;
    Vec2d<pxs> kern(GlyphIndex previous) const;
    optional<Box<pxs>> getGlyphBBox() const;
};

class TextContext {
    Handle<FT_Library> library_;

  public:
    static std::tuple<Error, TextContext> create();

    template <typename Buffer>
    std::tuple<Error, TextFace> loadFace(const Buffer& data, signed long face_index = 0) {
        std::tuple<Error, TextFace> result;
        auto& [rc, tface] = result;
        rc = Error::NotInitialized;
        if (!library_) return result;
        FT_Face face;
        auto error = FT_New_Memory_Face(*library_, data.data(), data.size(), face_index, &face);
        rc = Error::FaceNotLoaded;
        if (error) return result;
        tface.face_ = Handle(face);
        tface.hasKerning_ = FT_HAS_KERNING(face);
        rc = Error::OK;
        return result;
    }
};

std::tuple<Error, Box<pxs>, UTF8Range<const char*>>
findLongestWordSequence(TextFace* face, const UTF8Range<const char*>& text,
                        const Box<pxs>& boundingBox);
Error drawText(TextFace* face, const UTF8Range<const char*>& text, const PixelDrawer& drawPixel,
               PxPoint pen);

}  //  namespace teeui

#endif  // TEEUI_LIBTEEUI_FONT_RENDERING_H_
