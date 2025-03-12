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

#ifndef LIBTEEUI_BUTTON_H_
#define LIBTEEUI_BUTTON_H_

#include "utils.h"

// #define DRAW_DEBUG_MARKERS

namespace teeui {

class ButtonImpl {

  public:
    struct ConvexObjectInfo {
        const PxPoint* begin;
        const PxPoint* end;
    };

    ButtonImpl()
        : radius_(0), color_(0), convexObjectColor_(0), roundTopLeft_(false), roundTopRight_(false),
          roundBottomLeft_(false), roundBottomRight_(false) {}
    ButtonImpl(pxs radius, Color color, Color convexObjectColor, bool roundTopLeft,
               bool roundTopRight, bool roundBottomLeft, bool roundBottomRight)
        : radius_(radius), color_(color), convexObjectColor_(convexObjectColor),
          roundTopLeft_(roundTopLeft), roundTopRight_(roundTopRight),
          roundBottomLeft_(roundBottomLeft), roundBottomRight_(roundBottomRight) {}

    void setColor(Color color) { color_ = color; }
    void setConvexObjectColor(Color color) { convexObjectColor_ = color; }

    Error draw(const PixelDrawer& drawPixel, const Box<pxs>& bounds,
               const ConvexObjectInfo* coBegin, const ConvexObjectInfo* coEnd);

  private:
    pxs radius_;
    Color color_;
    Color convexObjectColor_;
    bool roundTopLeft_;
    bool roundTopRight_;
    bool roundBottomLeft_;
    bool roundBottomRight_;
};

/**
 * Button is a LayoutElement and should be used as second argument in the BEGIN_ELEMENT() macro.
 * The template argument Derived is the new class derived from Button, that is created by the
 * BEGIN_ELEMENT() macro. The arguments convexElemCount and convexObjectCapacity denote the number
 * of convex objects and the capacity (maximum number of vertexes) each of the objects should have.
 * This is used to reserve enough space at compile time.
 */
template <typename Derived, typename convexElemCount = std::integral_constant<size_t, 0>,
          typename convexObjectCapacity = std::integral_constant<size_t, 10>>
class Button : public LayoutElement<Derived>, public ButtonImpl {
  public:
    static const constexpr bool button_round_top_left = false;
    static const constexpr bool button_round_top_right = false;
    static const constexpr bool button_round_bottom_left = false;
    static const constexpr bool button_round_bottom_right = false;
    static const constexpr std::tuple<> button_drawable_objects = {};
    static const constexpr Color button_drawable_object_color = 0xff000000;

  private:
    ConvexObject<convexObjectCapacity::value> convex_objects_[convexElemCount::value];

  public:
    Button() = default;
    template <typename Context>
    explicit Button(const Context& context)
        : LayoutElement<Derived>(context),
          ButtonImpl(context = Derived::button_radius, context = Derived::button_color,
                     context = Derived::button_drawable_object_color,
                     Derived::button_round_top_left, Derived::button_round_top_right,
                     Derived::button_round_bottom_left, Derived::button_round_bottom_right) {
        static_assert(
            convexElemCount::value >=
                std::tuple_size<decltype(Derived::button_drawable_objects)>::value,
            "Reserved convex element count must be greater or equal to the number of given convex "
            "objects. Set count by passing ConvexObjectCount(n) to BEGIN_ELEMENT as third "
            "argument");
        initConvexObjectArray(context, convex_objects_, Derived::button_drawable_objects);
    }

    Error draw(const PixelDrawer& drawPixel) {
        constexpr const size_t convex_object_count =
            std::tuple_size<decltype(Derived::button_drawable_objects)>::value;
        ButtonImpl::ConvexObjectInfo coInfo[convex_object_count];
        for (size_t i = 0; i < convex_object_count; ++i) {
            coInfo[i].begin = convex_objects_[i].begin();
            coInfo[i].end = convex_objects_[i].end();
        }
        return ButtonImpl::draw(drawPixel, this->bounds_, &coInfo[0], &coInfo[convex_object_count]);
    }
};

}  //  namespace teeui

#define CornerRadius(radius) static const constexpr auto button_radius = radius

#define ButtonColor(color) static const constexpr auto button_color = color

#define RoundTopLeft static const constexpr bool button_round_top_left = true
#define RoundTopRight static const constexpr bool button_round_top_right = true
#define RoundBottomLeft static const constexpr bool button_round_bottom_left = true
#define RoundBottomRight static const constexpr bool button_round_bottom_right = true

/*
 * ConvexObjecCount may be passed to BEGIN_ELEMENT as third argument to layout elements that
 * draw convex objects, such as teeui::Button. It informs the underlying implementation
 * how much memory to reserve for convex objects.
 */
#define ConvexObjectCount(n) std::integral_constant<size_t, n>

#define ConvexObjects(convex_objects)                                                              \
    static constexpr const auto button_drawable_objects = convex_objects

#define ConvexObjectColor(color) static constexpr const auto button_drawable_object_color = color

#endif  // LIBTEEUI_BUTTON_H_
