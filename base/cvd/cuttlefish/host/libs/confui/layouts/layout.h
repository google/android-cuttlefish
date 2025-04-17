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
DECLARE_PARAMETER(DefaultFontSize);  // dps(14) regular and dps(18) magnified
DECLARE_PARAMETER(BodyFontSize);     // dps(16) regular and dps(20) magnified
DECLARE_TYPED_PARAMETER(ShieldColor, ::teeui::Color);
DECLARE_TYPED_PARAMETER(ColorText, ::teeui::Color);
DECLARE_TYPED_PARAMETER(ColorBG, ::teeui::Color);

CONSTANT(BorderWidth, dps(24));

DECLARE_FONT_BUFFER(RobotoMedium, RobotoMedium, RobotoMedium_length);
DECLARE_FONT_BUFFER(RobotoRegular, RobotoRegular, RobotoRegular_length);
DECLARE_FONT_BUFFER(Shield, Shield, Shield_length);

CONSTANT(DefaultFont, FONT(RobotoRegular));

DECLARE_TYPED_PARAMETER(ColorButton, ::teeui::Color);

NEW_PARAMETER_SET(ConfUIParameters, RightEdgeOfScreen, BottomOfScreen,
                  DefaultFontSize, BodyFontSize, ShieldColor, ColorText,
                  ColorBG, ColorButton);

CONSTANT(IconShieldDistanceFromTop, dps(100));
CONSTANT(LabelBorderZone, dps(4));
CONSTANT(RightLabelEdge, RightEdgeOfScreen() - BorderWidth);
CONSTANT(LabelWidth, RightLabelEdge - BorderWidth);
CONSTANT(ButtonHeight, dps(72));
CONSTANT(ButtonPositionX, 0);
CONSTANT(ButtonPositionY, BottomOfScreen() - ButtonHeight);
CONSTANT(ButtonWidth, dps(130));
CONSTANT(ButtonLabelDistance, dps(12));

BEGIN_ELEMENT(IconShield, teeui::Label)
FontSize(dps(24));
LineHeight(dps(24));
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
FontSize(dps(20));
LineHeight(dps(20));
NumberOfLines(1);
Dimension(LabelWidth, HeightFromLines);
Position(BorderWidth, BOTTOM_EDGE_OF(IconShield) + dps(16));
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
CornerRadius(dps(4));
ButtonColor(ColorButton());
RoundTopLeft;
RoundBottomLeft;
RoundTopRight;
RoundBottomRight;
END_ELEMENT();

BEGIN_ELEMENT(LabelOK, teeui::Label)
FontSize(BodyFontSize());
LineHeight(BodyFontSize() * pxs(1.4));
NumberOfLines(1);
Dimension(ButtonWidth - (LabelBorderZone * dps(2)),
          ButtonHeight - BorderWidth - (LabelBorderZone * dps(2)));
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
LineHeight(BodyFontSize() * pxs(1.4));
NumberOfLines(1);
Dimension(ButtonWidth - (LabelBorderZone * dps(2)),
          ButtonHeight - BorderWidth - (LabelBorderZone * dps(2)));
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
LineHeight(DefaultFontSize() * pxs(1.5));
NumberOfLines(4);
Dimension(LabelWidth, HeightFromLines);
Position(BorderWidth, ButtonPositionY - dim_h - dps(48));
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
LineHeight(BodyFontSize() * pxs(1.4));
NumberOfLines(20);
Position(BorderWidth, BOTTOM_EDGE_OF(LabelTitle) + dps(16));
Dimension(LabelWidth, LabelHint::pos_y - pos_y - dps(24));
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
