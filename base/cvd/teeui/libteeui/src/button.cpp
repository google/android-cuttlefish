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

#include <teeui/button.h>
namespace teeui {

ConvexObject<4> makeSquareAtOffset(const PxPoint& offset, const pxs& sideLength) {
    return ConvexObject<4>({offset, offset + PxPoint{sideLength, .0},
                            offset + PxPoint{sideLength, sideLength},
                            offset + PxPoint{.0, sideLength}});
}

Error ButtonImpl::draw(const PixelDrawer& drawPixel, const Box<pxs>& bounds,
                       const ConvexObjectInfo* coBegin, const ConvexObjectInfo* coEnd) {

    using intpxs = Coordinate<px, int64_t>;
    Box<intpxs> intBounds(bounds);

    auto drawPixelBoundsEnforced = [&](uint32_t x, uint32_t y, Color color) -> Error {
        if (!intBounds.contains(Point<intpxs>(x, y))) {
            TEEUI_LOG << "Bounds: " << bounds << " Pixel: " << Point<pxs>(x, y) << ENDL;
            return Error::OutOfBoundsDrawing;
        }
        return drawPixel(x, y, color);
    };

    auto drawBox = [&](const Box<intpxs>& box, Color c) -> Error {
        for (int y = 0; y < box.h().count(); ++y) {
            for (int x = 0; x < box.w().count(); ++x) {
                if (auto error = drawPixel(box.x().count() + x, box.y().count() + y, c)) {
                    return error;
                }
            }
        }
        return Error::OK;
    };

#ifdef DRAW_DEBUG_MARKERS
    auto drawDebugBox = [&](const Box<pxs>& box, Color c) {
        drawBox(box, (c & 0xffffff) | 0x40000000);
    };

    drawDebugBox(intBounds, 0xff);
#endif

    intpxs intRadius = radius_.count();

    TEEUI_LOG << intBounds << ENDL;

    auto drawCorner = [&, this](intpxs right, intpxs bottom) -> Error {
        Box<intpxs> cBounds(intBounds.x(), intBounds.y(), intRadius, intRadius);
        cBounds.translateSelf(Point<intpxs>(right * (intBounds.w() - intRadius),
                                            bottom * (intBounds.h() - intRadius)));
        auto center = Point<pxs>((intpxs(1) - right) * intRadius, (intpxs(1) - bottom) * intRadius);
        center += cBounds.topLeft();
        center -= Point<pxs>(.5, .5);
        TEEUI_LOG << "Radius: " << intRadius << " cBounds: " << cBounds << " center: " << center
                  << ENDL;
        for (int y = 0; y < cBounds.h().count(); ++y) {
            for (int x = 0; x < cBounds.w().count(); ++x) {
                auto pos = Point<pxs>(cBounds.x().count() + x, cBounds.y().count() + y);
                auto color = drawCirclePoint(center, intRadius, pos, color_);
                if (auto error = drawPixelBoundsEnforced(pos.x().count(), pos.y().count(), color)) {
                    return error;
                }
            }
        }
        return Error::OK;
    };

    if (roundTopLeft_) {
        if (auto error = drawCorner(0, 0)) return error;
    } else {
        if (auto error = drawBox(
                Box<intpxs>(0, 0, intRadius, intRadius).translate(intBounds.topLeft()), color_)) {
            return error;
        }
    }

    if (roundTopRight_) {
        if (auto error = drawCorner(1, 0)) return error;
    } else {
        if (auto error = drawBox(Box<intpxs>(intBounds.w() - intRadius, 0, intRadius, intRadius)
                                     .translate(intBounds.topLeft()),
                                 color_)) {
            return error;
        }
    }

    if (roundBottomLeft_) {
        if (auto error = drawCorner(0, 1)) return error;
    } else {
        if (auto error = drawBox(Box<intpxs>(0, intBounds.h() - intRadius, intRadius, intRadius)
                                     .translate(intBounds.topLeft()),
                                 color_)) {
            return error;
        }
    }

    if (roundBottomRight_) {
        if (auto error = drawCorner(1, 1)) return error;
    } else {
        if (auto error = drawBox(Box<intpxs>(intBounds.w() - intRadius, intBounds.h() - intRadius,
                                             intRadius, intRadius)
                                     .translate(intBounds.topLeft()),
                                 color_)) {
            return error;
        }
    }

    auto centerbox = Box<intpxs>(intRadius, intRadius, intBounds.w() - intRadius - intRadius,
                                 intBounds.h() - intRadius - intRadius)
                         .translate(intBounds.topLeft());

    if (auto error = drawBox(centerbox, color_)) return error;

    if (auto error =
            drawBox(Box<intpxs>(0, intRadius, intRadius, intBounds.h() - intRadius - intRadius)
                        .translate(intBounds.topLeft()),
                    color_)) {
        return error;
    }
    if (auto error =
            drawBox(Box<intpxs>(intRadius, 0, intBounds.w() - intRadius - intRadius, intRadius)
                        .translate(intBounds.topLeft()),
                    color_)) {
        return error;
    }
    if (auto error = drawBox(Box<intpxs>(intBounds.w() - intRadius, intRadius, intRadius,
                                         intBounds.h() - intRadius - intRadius)
                                 .translate(intBounds.topLeft()),
                             color_)) {
        return error;
    }
    if (auto error = drawBox(Box<intpxs>(intRadius, intBounds.h() - intRadius,
                                         intBounds.w() - intRadius - intRadius, intRadius)
                                 .translate(intBounds.topLeft()),
                             color_)) {
        return error;
    }

    bool hasCOs = coBegin != coEnd;
    if (hasCOs) {
        Box<pxs> coBBox = Box<pxs>::boundingBox(coBegin->begin, coBegin->end);
        for (const auto& co : makeRange(coBegin + 1, coEnd)) {
            coBBox = coBBox.merge(co.begin, co.end);
        }

        auto start = PxPoint{bounds.w() - coBBox.w(), bounds.h() - coBBox.h()} / pxs(2.0);
        start += bounds.topLeft();

        Box<intpxs> intBBox(start.x().floor(), start.y().floor(), 0, 0);
        intBBox = intBBox.merge(
            Point<intpxs>{(start.x() + coBBox.w()).ceil(), (start.y() + coBBox.h()).ceil()});

        TEEUI_LOG << "coBBox: " << coBBox << ENDL;

        TEEUI_LOG << "intBBox: " << intBBox << ENDL;
        for (int64_t y = 0; y < intBBox.h().count(); ++y) {
            for (int64_t x = 0; x < intBBox.w().count(); ++x) {
                PxPoint offset = coBBox.topLeft() + PxPoint{x, y};
                // The pixel is a square of width and height 1.0
                auto thePixel = makeSquareAtOffset(offset, 1.0);
                TEEUI_LOG << thePixel << ENDL;
                pxs areaCovered = 0.0;
                for (const auto& co : makeRange(coBegin, coEnd)) {
                    auto coveredRegion = thePixel.intersect<10>(co.begin, co.end);
                    if (coveredRegion) areaCovered += coveredRegion->area();
                    TEEUI_LOG << " region: " << (bool)coveredRegion << " area: " << areaCovered;
                }
                TEEUI_LOG << ENDL;
                if (areaCovered > 1.0) areaCovered = 1.0;
                TEEUI_LOG << "x: " << x << " y: " << y << " area: " << areaCovered << ENDL;
                uint32_t intensity = 0xff * areaCovered.count();
                if (auto error =
                        drawPixelBoundsEnforced(intBBox.x().count() + x, intBBox.y().count() + y,
                                                intensity << 24 | (0xffffff & convexObjectColor_)))
                    return error;
            }
        }
    }

    return Error::OK;
}

}  // namespace teeui
