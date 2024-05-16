/*
 * Copyright 2021, The Android Open Source Project
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

#pragma once

#include <teeui/incfont.h>

/*
 * Each entry TEEUI_INCFONT(<name>) declares:
 *    extern unsigned char <name>[];
 *    extern unsigned int <name>_length;
 * The first one pointing to a raw ttf font file in the .rodata section, and the
 * second being the size of the buffer.
 */
TEEUI_INCFONT(RobotoMedium);
TEEUI_INCFONT(RobotoRegular);
TEEUI_INCFONT(Shield);
