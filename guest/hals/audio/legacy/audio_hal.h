/*
 * Copyright (C) 2016 The Android Open Source Project
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

#define AUDIO_DEBUG 0

#if AUDIO_DEBUG
#  define D(...) ALOGD(__VA_ARGS__)
#else
#  define D(...) ((void)0)
#endif

#include <errno.h>
#include <string.h>
#include <log/log.h>

#include <hardware/hardware.h>

#include "guest/libs/platform_support/api_level_fixes.h"

#pragma GCC diagnostic push
#pragma  GCC diagnostic ignored "-Wparentheses"
#if VSOC_PLATFORM_SDK_AFTER(K)
#pragma  GCC diagnostic ignored "-Wgnu-designator"
#endif
#include <system/audio.h>
#pragma GCC diagnostic pop

#include <hardware/audio.h>
