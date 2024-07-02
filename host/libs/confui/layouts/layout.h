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
DECLARE_PARAMETER(DefaultFontSize);  // 14_dp regular and 18_dp magnified
DECLARE_PARAMETER(BodyFontSize);     // 16_dp regular and 20_dp magnified
DECLARE_TYPED_PARAMETER(ShieldColor, ::teeui::Color);
DECLARE_TYPED_PARAMETER(ColorText, ::teeui::Color);
DECLARE_TYPED_PARAMETER(ColorBG, ::teeui::Color);

CONSTANT(BorderWidth, 24_dp);

DECLARE_FONT_BUFFER(RobotoMedium, RobotoMedium, RobotoMedium_length);
DECLARE_FONT_BUFFER(RobotoRegular, RobotoRegular, RobotoRegular_length);
DECLARE_FONT_BUFFER(Shield, Shield, Shield_length);

CONSTANT(DefaultFont, FONT(RobotoRegular));

DECLARE_TYPED_PARAMETER(ColorButton, ::teeui::Color);

NEW_PARAMETER_SET(ConfUIParameters, RightEdgeOfScreen, BottomOfScreen,
                  DefaultFontSize, BodyFontSize, ShieldColor, ColorText,
                  ColorBG, ColorButton);

CONSTANT(IconShieldDistanceFromTop, 100_dp);
CONSTANT(LabelBorderZone, 4_dp);
CONSTANT(RightLabelEdge, RightEdgeOfScreen() - BorderWidth);
CONSTANT(LabelWidth, RightLabelEdge - BorderWidth);
CONSTANT(ButtonHeight, 72_dp);
CONSTANT(ButtonPositionX, 0);
CONSTANT(ButtonPositionY, BottomOfScreen() - ButtonHeight);
CONSTANT(ButtonWidth, 130_dp);
CONSTANT(ButtonLabelDistance, 12_dp);

BEGIN_ELEMENT(IconShield, teeui::Label)
FontSize(24_dp);
LineHeight(24_dp);
NumberOfLines(1);
Dimension(LabelWidth, HeightFromLines);
Position(BorderWidth, IconShieldDistanceFromTop);
DefaultText(
    "A");  // ShieldTTF has just one glyph at the code point for capital A
TextColor(ShieldColor());
HorizontalTextAlignment(Alignment::CENTER);
Font(FONT(Shield));
END_ELEMENT();

BEGIN_ELEMENT(LabelTitle, teeui::Label)
FontSize(20_dp);
LineHeight(20_dp);
NumberOfLines(1);
Dimension(LabelWidth, HeightFromLines);
Position(BorderWidth, BOTTOM_EDGE_OF(IconShield) + 16_dp);
DefaultText("Android Protected Confirmation");
Font(FONT(RobotoMedium));
VerticallyCentered;
TextColor(ColorText());
TextID(TEXT_ID(TranslationId::TITLE));
END_ELEMENT();

BEGIN_ELEMENT(IconOk, teeui::Button, ConvexObjectCount(1))
Dimension(ButtonWidth, ButtonHeight - BorderWidth);
Position(RightEdgeOfScreen() - ButtonWidth - BorderWidth,
         ButtonPositionY + ButtonLabelDistance);
CornerRadius(4_dp);
ButtonColor(ColorButton());
RoundTopLeft;
RoundBottomLeft;
RoundTopRight;
RoundBottomRight;
END_ELEMENT();

BEGIN_ELEMENT(LabelOK, teeui::Label)
FontSize(BodyFontSize());
LineHeight(BodyFontSize() * 1.4_px);
NumberOfLines(1);
Dimension(ButtonWidth - (LabelBorderZone * 2_dp),
          ButtonHeight - BorderWidth - (LabelBorderZone * 2_dp));
Position(RightEdgeOfScreen() - ButtonWidth - BorderWidth + LabelBorderZone,
         ButtonPositionY + ButtonLabelDistance + LabelBorderZone);
DefaultText("Confirm");
Font(FONT(RobotoMedium));
HorizontalTextAlignment(Alignment::CENTER);
VerticalTextAlignment(Alignment::CENTER);
TextColor(ColorBG());
TextID(TEXT_ID(TranslationId::CONFIRM));
END_ELEMENT();

BEGIN_ELEMENT(LabelCancel, teeui::Label)
FontSize(BodyFontSize());
LineHeight(BodyFontSize() * 1.4_px);
NumberOfLines(1);
Dimension(ButtonWidth - (LabelBorderZone * 2_dp),
          ButtonHeight - BorderWidth - (LabelBorderZone * 2_dp));
Position(BorderWidth + LabelBorderZone,
         ButtonPositionY + ButtonLabelDistance + LabelBorderZone);
DefaultText("Cancel");
HorizontalTextAlignment(Alignment::LEFT);
Font(FONT(RobotoMedium));
VerticallyCentered;
TextColor(ColorButton());
TextID(TEXT_ID(TranslationId::CANCEL));
END_ELEMENT();

BEGIN_ELEMENT(LabelHint, teeui::Label)
FontSize(DefaultFontSize());
LineHeight(DefaultFontSize() * 1.5_px);
NumberOfLines(4);
Dimension(LabelWidth, HeightFromLines);
Position(BorderWidth, ButtonPositionY - dim_h - 48_dp);
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
Position(BorderWidth, BOTTOM_EDGE_OF(LabelTitle) + 16_dp);
Dimension(LabelWidth, LabelHint::pos_y - pos_y - 24_dp);
DefaultText(
    "12345678901234567890123456789012345678901234567890123456789012345678901234"
    "567890123456"
    "78901234567890");
TextColor(ColorText());
Font(FONT(RobotoRegular));
END_ELEMENT();

NEW_LAYOUT(ConfUILayout, IconShield, LabelTitle, LabelHint, LabelBody, IconOk,
           LabelOK, LabelCancel);

}  // namespace teeui
