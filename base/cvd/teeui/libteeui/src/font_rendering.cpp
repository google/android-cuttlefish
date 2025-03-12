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

#include <teeui/font_rendering.h>

namespace teeui {

bool isBreakable(unsigned long codePoint) {
    switch (codePoint) {
    case 9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD:
    case 0x20:
    case 0x85:
    case 0x1680:  // Ogham Space Mark
    case 0x180E:  // Mongolian Vowel Separator
    case 0x2000:  // EN Quad
    case 0x2001:  // EM Quad
    case 0x2002:  // EN Space
    case 0x2003:  // EM Space
    case 0x2004:  // three per em space
    case 0x2005:  // four per em space
    case 0x2006:  // six per em space
    case 0x2008:  // Punctuation space
    case 0x2009:  // Thin Space
    case 0x200A:  // Hair Space
    case 0x200B:  // Zero Width Space
    case 0x200C:  // Zero Width Non-Joiner
    case 0x200D:  // Zero Width Joiner
    case 0x2028:  // Line Separator
    case 0x2029:  // Paragraph Separator
    case 0x205F:  // Medium Mathematical Space
    case 0x3000:  // Ideographic Space
        return true;
    default:
        return false;
    }
}

bool isNewline(unsigned long codePoint) {
    return codePoint == '\n';
}

Error TextFace::setCharSize(signed long char_size, unsigned int dpi) {
    if (!face_) return Error::NotInitialized;
    auto error = FT_Set_Char_Size(*face_, 0, char_size, 0, dpi);
    if (error) return Error::CharSizeNotSet;
    return Error::OK;
}

Error TextFace::setCharSizeInPix(pxs size) {
    if (!face_) return Error::NotInitialized;
    if (FT_Set_Pixel_Sizes(*face_, 0, size.count())) {
        return Error::CharSizeNotSet;
    }
    return Error::OK;
}

GlyphIndex TextFace::getCharIndex(unsigned long codePoint) {
    if (!face_) return 0;
    return FT_Get_Char_Index(*face_, codePoint);
}

Error TextFace::loadGlyph(GlyphIndex index) {
    if (!face_) return Error::NotInitialized;
    if (FT_Load_Glyph(*face_, index, FT_LOAD_DEFAULT)) {
        return Error::GlyphNotLoaded;
    }
    return Error::OK;
}

Error TextFace::renderGlyph() {
    if (!face_) return Error::NotInitialized;
    if (FT_Render_Glyph(face_->glyph, FT_RENDER_MODE_NORMAL)) {
        return Error::GlyphNotRendered;
    }
    return Error::OK;
}

Vec2d<pxs> TextFace::advance() const {
    return Vec2d<pxs>(face_->glyph->advance.x / 64.0, face_->glyph->advance.y / 64.0);
}

Vec2d<pxs> TextFace::kern(GlyphIndex previous) const {
    FT_Vector offset = {0, 0};
    if (hasKerning_ && previous) {
        if (!FT_Get_Kerning(*face_, previous, face_->glyph->glyph_index, FT_KERNING_DEFAULT,
                            &offset)) {
            offset = {0, 0};
        }
    }
    return {offset.x / 64.0, offset.y / 64.0};
}

optional<Box<pxs>> TextFace::getGlyphBBox() const {
    FT_Glyph glyph;
    if (!FT_Get_Glyph(face_->glyph, &glyph)) {
        FT_BBox cbox;
        FT_Glyph_Get_CBox(glyph, ft_glyph_bbox_pixels, &cbox);
        FT_Done_Glyph(glyph);
        return {{cbox.xMin, -cbox.yMax, cbox.xMax - cbox.xMin, cbox.yMax - cbox.yMin}};
    }
    return {};
}

std::tuple<Error, TextContext> TextContext::create() {
    std::tuple<Error, TextContext> result;
    auto& [rc, lib] = result;
    rc = Error::NotInitialized;
    FT_Library library = nullptr;
    auto error = FT_Init_FreeType(&library);
    if (error) return result;
    rc = Error::OK;
    lib.library_ = Handle(library);
    return result;
}

std::tuple<Error, Box<pxs>, UTF8Range<const char*>>
findLongestWordSequence(TextFace* face, const UTF8Range<const char*>& text,
                        const Box<pxs>& boundingBox) {
    std::tuple<Error, Box<pxs>, UTF8Range<const char*>> result;
    auto& [error, bBox, resultRange] = result;

    Vec2d<pxs> pen = {0, 0};
    bBox = {pen, {0, 0}};

    GlyphIndex previous = 0;
    UTF8WordRange<const char*> wordRange(text);
    auto rangeBegin = wordRange.begin();
    if (isBreakable((*rangeBegin).codePoint())) {
        ++rangeBegin;
    }
    auto wordEnd = rangeBegin;
    auto wordStart = wordEnd;
    ++wordEnd;
    auto sequenceEnd = *wordStart;
    auto lastBreakableBegin = *wordStart;
    Box<pxs> currentBox = bBox;
    Box<pxs> currentFullWordBox = bBox;
    while (wordStart != wordRange.end()) {
        auto codePoint = UTF8Range<const char*>::codePoint(**wordStart);
        if (isBreakable(codePoint)) {
            lastBreakableBegin = *wordStart;
            currentFullWordBox = currentBox;
        }

        Box<pxs> workingBox = currentBox;
        auto c = *wordStart;
        bool exceedsBoundingBox = false;
        while (c != *wordEnd) {
            codePoint = c.codePoint();
            auto gindex = face->getCharIndex(codePoint);
            if (gindex == 0) {
                error = Error::GlyphNotLoaded;
                return result;
            }
            error = face->loadGlyph(gindex);
            if (error != Error::OK) return result;
            pen += face->kern(previous);
            if (auto gBox = face->getGlyphBBox()) {
                TEEUI_LOG << "Glyph Box: " << *gBox << ENDL;
                workingBox = workingBox.merge(gBox->translateSelf(pen));
                TEEUI_LOG << "WorkingBox: " << workingBox << ENDL;
            } else {
                error = Error::BBoxComputation;
                return result;
            }
            pen += face->advance();
            previous = gindex;
            ++c;
            if (workingBox.fitsInside(boundingBox)) {
                currentBox = workingBox;
                sequenceEnd = c;
            } else {
                exceedsBoundingBox = true;
                TEEUI_LOG << "exceeding bbox" << ENDL;
                break;
            }
        }
        if (exceedsBoundingBox) break;
        wordStart = wordEnd;
        ++wordEnd;
    }
    if (wordStart == wordRange.end()) {
        bBox = currentBox;
        resultRange = {**rangeBegin, *text.end()};
        TEEUI_LOG << "full range" << ENDL;
    } else if (*rangeBegin != lastBreakableBegin) {
        bBox = currentFullWordBox;
        resultRange = {**rangeBegin, *lastBreakableBegin};
        TEEUI_LOG << "partial range:" << ENDL;
    } else {
        bBox = currentBox;
        resultRange = {**rangeBegin, *sequenceEnd};
        TEEUI_LOG << "unbreakable" << ENDL;
    }
    error = Error::OK;
    return result;
}

Error drawText(TextFace* face, const UTF8Range<const char*>& text, const PixelDrawer& drawPixel,
               PxPoint pen) {
    Error error;

    for (auto c : text) {
        auto codePoint = UTF8Range<const char*>::codePoint(c);
        auto gindex = face->getCharIndex(codePoint);
        error = face->loadGlyph(gindex);
        if (error == Error::OK) error = face->renderGlyph();
        if (error == Error::OK) error = face->drawGlyph(pen, drawPixel);
        if (error != Error::OK) return error;

        pen += face->advance();
    }
    return Error::OK;
}

}  //  namespace teeui
