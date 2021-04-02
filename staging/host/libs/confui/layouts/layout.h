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

#include <teeui/button.h>
#include <teeui/label.h>
#include <teeui/localization/ConfirmationUITranslations.h>
#include <teeui/utils.h>

#include "fonts.h"

using teeui::localization::TranslationId;

namespace teeui {

DECLARE_PARAMETER(RightEdgeOfScreen);
DECLARE_PARAMETER(BottomOfScreen);
DECLARE_PARAMETER(PowerButtonTop);
DECLARE_PARAMETER(PowerButtonBottom);
DECLARE_PARAMETER(VolUpButtonTop);
DECLARE_PARAMETER(VolUpButtonBottom);
DECLARE_PARAMETER(DefaultFontSize);  // 14_dp regular and 18_dp magnified
DECLARE_PARAMETER(BodyFontSize);     // 16_dp regular and 20_dp magnified
DECLARE_TYPED_PARAMETER(ShieldColor, ::teeui::Color);
DECLARE_TYPED_PARAMETER(ColorText, ::teeui::Color);
DECLARE_TYPED_PARAMETER(ColorBG, ::teeui::Color);

NEW_PARAMETER_SET(ConUIParameters, RightEdgeOfScreen, BottomOfScreen,
                  PowerButtonTop, PowerButtonBottom, VolUpButtonTop,
                  VolUpButtonBottom, DefaultFontSize, BodyFontSize, ShieldColor,
                  ColorText, ColorBG);

CONSTANT(BorderWidth, 24_dp);
CONSTANT(PowerButtonCenter, (PowerButtonTop() + PowerButtonBottom()) / 2_px);
CONSTANT(VolUpButtonCenter, (VolUpButtonTop() + VolUpButtonBottom()) / 2.0_px);
CONSTANT(GrayZone, 12_dp);
CONSTANT(RightLabelEdge, RightEdgeOfScreen() - BorderWidth - GrayZone);
CONSTANT(LabelWidth, RightLabelEdge - BorderWidth);

CONSTANT(SQRT2, 1.4142135623_dp);
CONSTANT(SQRT8, 2.828427125_dp);

CONSTANT(ARROW_SHAPE,
         CONVEX_OBJECTS(
             CONVEX_OBJECT(Vec2d{.0_dp, .0_dp}, Vec2d{6.0_dp, 6.0_dp},
                           Vec2d{6.0_dp - SQRT8, 6.0_dp}, Vec2d{-SQRT2, SQRT2}),
             CONVEX_OBJECT(Vec2d{6.0_dp - SQRT8, 6.0_dp}, Vec2d{6.0_dp, 6.0_dp},
                           Vec2d{0.0_dp, 12.0_dp},
                           Vec2d{-SQRT2, 12.0_dp - SQRT2})));

DECLARE_FONT_BUFFER(RobotoMedium, RobotoMedium, RobotoMedium_length);
DECLARE_FONT_BUFFER(RobotoRegular, RobotoRegular, RobotoRegular_length);
DECLARE_FONT_BUFFER(Shield, Shield, Shield_length);

CONSTANT(DefaultFont, FONT(RobotoRegular));

BEGIN_ELEMENT(LabelOK, teeui::Label)
FontSize(DefaultFontSize());
LineHeight(20_dp);
NumberOfLines(2);
Dimension(LabelWidth, HeightFromLines);
Position(BorderWidth, PowerButtonCenter - dim_h / 2.0_px);
DefaultText("Press Power Button Confirm");
RightJustified;
VerticallyCentered;
TextColor(ColorText());
Font(FONT(RobotoMedium));
TextID(TEXT_ID(TranslationId::CONFIRM_PWR_BUTTON_DOUBLE_PRESS));
END_ELEMENT();

BEGIN_ELEMENT(IconPower, teeui::Button, ConvexObjectCount(2))
Dimension(BorderWidth, PowerButtonBottom() - PowerButtonTop());
Position(RightEdgeOfScreen() - BorderWidth, PowerButtonTop());
CornerRadius(3_dp);
ButtonColor(ColorText());
RoundTopLeft;
RoundBottomLeft;
ConvexObjectColor(ColorBG());
ConvexObjects(ARROW_SHAPE);
END_ELEMENT();

BEGIN_ELEMENT(LabelCancel, teeui::Label)
FontSize(DefaultFontSize());
LineHeight(20_dp);
NumberOfLines(2);
Dimension(LabelWidth, HeightFromLines);
Position(BorderWidth, VolUpButtonCenter - dim_h / 2.0_px);
DefaultText("Press Menu Button to Cancel");
RightJustified;
VerticallyCentered;
TextColor(ColorText());
Font(FONT(RobotoMedium));
TextID(TEXT_ID(TranslationId::CANCEL));
END_ELEMENT();

BEGIN_ELEMENT(IconVolUp, teeui::Button, ConvexObjectCount(2))
Dimension(BorderWidth, VolUpButtonBottom() - VolUpButtonTop());
Position(RightEdgeOfScreen() - BorderWidth, VolUpButtonTop());
CornerRadius(5_dp);
ButtonColor(ColorBG());
ConvexObjectColor(ColorText());
ConvexObjects(ARROW_SHAPE);
END_ELEMENT();

BEGIN_ELEMENT(IconShield, teeui::Label)
FontSize(24_dp);
LineHeight(24_dp);
NumberOfLines(1);
Dimension(LabelWidth, HeightFromLines);
Position(BorderWidth, BOTTOM_EDGE_OF(LabelCancel) + 40_dp);
DefaultText(
    "A");  // ShieldTTF has just one glyph at the code point for capital A
TextColor(ShieldColor());
Font(FONT(Shield));
END_ELEMENT();

BEGIN_ELEMENT(LabelTitle, teeui::Label)
FontSize(20_dp);
LineHeight(20_dp);
NumberOfLines(1);
Dimension(RightEdgeOfScreen() - BorderWidth, HeightFromLines);
Position(BorderWidth, BOTTOM_EDGE_OF(IconShield) + 12_dp);
DefaultText("Android Protected Confirmation");
Font(FONT(RobotoMedium));
VerticallyCentered;
TextColor(ColorText());
TextID(TEXT_ID(TranslationId::TITLE));
END_ELEMENT();

BEGIN_ELEMENT(LabelHint, teeui::Label)
FontSize(DefaultFontSize());
LineHeight(DefaultFontSize() * 1.5_px);
NumberOfLines(4);
Dimension(LabelWidth, HeightFromLines);
Position(BorderWidth, BottomOfScreen() - BorderWidth - dim_h);
DefaultText(
    "This confirmation provides an extra layer of security for the action "
    "you're "
    "about to take.");
VerticalTextAlignment(Alignment::BOTTOM);
TextColor(ColorText());
Font(DefaultFont);
TextID(TEXT_ID(TranslationId::DESCRIPTION));
END_ELEMENT();

BEGIN_ELEMENT(LabelBody, teeui::Label)
FontSize(BodyFontSize());
LineHeight(BodyFontSize() * 1.4_px);
NumberOfLines(20);
Position(BorderWidth, BOTTOM_EDGE_OF(LabelTitle) + 18_dp);
Dimension(LabelWidth, LabelHint::pos_y - pos_y - 24_dp);
DefaultText(
    "12345678901234567890123456789012345678901234567890123456789012345678901234"
    "567890123456"
    "78901234567890");
TextColor(ColorText());
Font(FONT(RobotoRegular));
END_ELEMENT();

NEW_LAYOUT(ConfUILayout, LabelOK, IconPower, LabelCancel, IconVolUp, IconShield,
           LabelTitle, LabelHint, LabelBody);

}  // namespace teeui
