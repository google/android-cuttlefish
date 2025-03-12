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

#include <teeui/label.h>

#include <teeui/error.h>
#include <teeui/font_rendering.h>

namespace teeui {

Error LabelImpl::draw(const PixelDrawer& drawPixel, const Box<pxs>& bounds, LineInfo* lineInfo) {
    if (!font_) return Error::NotInitialized;

    Error error;
    TextContext context;
    std::tie(error, context) = TextContext::create();
    if (error) return error;

    TextFace face;
    std::tie(error, face) = context.loadFace(font_);
    if (error) return error;

    error = face.setCharSizeInPix(fontSize());
    if (error) return error;

    using intpxs = Coordinate<px, int64_t>;
    Box<intpxs> intBounds(intpxs((int64_t)bounds.x().count()), intpxs((int64_t)bounds.y().count()),
                          intpxs((int64_t)bounds.w().count()), intpxs((int64_t)bounds.h().count()));

    auto drawPixelBoundsEnforced =
        makePixelDrawer([&, this](uint32_t x, uint32_t y, Color color) -> Error {
            if (!intBounds.contains(Point<intpxs>(x, y))) {
                TEEUI_LOG << "Bounds: " << bounds << " Pixel: " << Point<pxs>(x, y) << ENDL;
                return Error::OutOfBoundsDrawing;
            }
            // combine the given alpha channel with the text color.
            return drawPixel(x, y, (textColor_ & 0xffffff) | (color & 0xff000000));
        });

#ifdef DRAW_DEBUG_MARKERS
    auto drawBox = [&](const Box<pxs>& box, Color c) {
        for (int y = 0; y < box.h().count(); ++y) {
            for (int x = 0; x < box.w().count(); ++x) {
                drawPixel(box.x().count() + x, box.y().count() + y, (c & 0xffffff) | 0x40000000);
            }
        }
    };

    drawBox(bounds, 0xff);
#endif

    Point<pxs> pen = {0_px, 0_px};
    auto textBegin = text_.begin();
    optional<Box<pxs>> boundingBox;

    auto curLine = lineInfo->begin();
    while (textBegin != text_.end()) {
        if (curLine == lineInfo->end()) {
            TEEUI_LOG << "lineInfo filled: lines=" << lineInfo->size_ << " textId=" << textId_
                      << ENDL;
            return Error::OutOfMemory;
        }

        auto lineEnd = textBegin;

        while (!isNewline(lineEnd.codePoint()) && lineEnd != text_.end()) {
            lineEnd++;
        }

        Box<pxs> bBox;
        std::tie(error, bBox, curLine->lineText) =
            findLongestWordSequence(&face, text_t(*textBegin, *lineEnd), bounds);
        if (error) return error;

        pen = {-bBox.x(), pen.y()};

        // check horizontal justification to set pen value
        switch (horizontalTextAlignment_) {
        case Alignment::LEFT:
        case Alignment::TOP:
        case Alignment::BOTTOM:
            break;
        case Alignment::CENTER:
            pen += {(bounds.w() - bBox.w()) / 2.0_px, 0};
            break;
        case Alignment::RIGHT:
            pen += {bounds.w() - bBox.w(), 0};
            break;
        }

        curLine->lineStart = pen;
        bBox.translateSelf(pen);

        if (boundingBox)
            boundingBox = boundingBox->merge(bBox);
        else
            boundingBox = bBox;

        // Set start position for next loop, skipping a newline if found
        textBegin = curLine->lineText.end();
        if (isNewline(textBegin.codePoint())) {
            textBegin++;
        }

        pen += {0_px, lineHeight_};
        ++curLine;
    }

    if (!boundingBox) return Error::OK;

    TEEUI_LOG << "BoundingBox: " << *boundingBox << " Bounds: " << bounds << ENDL;
    Point<pxs> offset = bounds.topLeft();
    offset -= {0, boundingBox->y()};
    TEEUI_LOG << "Offset: " << offset << ENDL;

    if (verticalTextAlignment_ == Alignment::CENTER)
        offset += {0, (bounds.h() - boundingBox->h()) / 2.0_px};
    else if (verticalTextAlignment_ == Alignment::BOTTOM)
        offset += {0, (bounds.h() - boundingBox->h())};

    auto lineEnd = curLine;
    curLine = lineInfo->begin();

#ifdef DRAW_DEBUG_MARKERS
    drawBox(boundingBox->translate(offset), 0xff00);
    auto p = offset + curLine->lineStart;
    drawPixel(p.x().count(), p.y().count(), 0xffff0000);
#endif

    while (curLine != lineEnd) {
        if (auto error = drawText(&face, curLine->lineText, drawPixelBoundsEnforced,
                                  curLine->lineStart + offset)) {
            TEEUI_LOG << "drawText returned " << error << ENDL;
            return error;
        }
        ++curLine;
    }
    return Error::OK;
}

Error LabelImpl::hit(const Event& event, const Box<pxs>& bounds) {
    using intpxs = Coordinate<px, int64_t>;
    if (bounds.contains(Point<intpxs>(event.x_, event.y_))) {
        optional<CallbackEvent> callback = getCB();
        if (callback) {
            return callback.value()(event);
        }
    }
    return Error::OK;
}
}  // namespace teeui
