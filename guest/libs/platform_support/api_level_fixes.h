/*
 * Copyright (C) 2017 The Android Open Source Project
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

// Fixes for various things that go wrong between Android versions.
// By convention this should be the very first include: it tweaks some
// flags that affect the processing of the system headers.
//
// Code that needs to cope with platform changes should use the
// VSOC_PLATFORM_SDK_BEFORE and VSOC_PLATFORM_SDK_AFTER macros below.
// It's fine to provide declarations for broadly used things in this file
// if that's easier.
//
// To use this header add $(VSOC_VERSION_CFLAGS) to the LOCAL_CFLAGS
// in the corresponding Android.mk. There is an error check to catch
// cases where this wasn't done.
//
// Code should not examine the SDK_PLATFORM_VERSION, and generally shouldn't
// look at the VSOC_PLATFORM_SDK_* values. While these currently track
// PLATFORM_SDK_VERSION, that's an implementation detail that will probably
// change: Android will eventually break things without bumping
// PLATFORM_SDK_VERSION.
//
// This is also why there is no SDK_PLATFORM_VERSION_IS(). Convert these
// statements into BEFORE and/or AFTER.
//
// To check for master/AOSP use VSOC_PLATFORM_VERSION_AFTER(LAST_SHIPPED)
#include <time.h>

#ifndef VSOC_PLATFORM_SDK_VERSION
#error VSOC_PLATFORM_SDK_VERSION is not set. Check your Android.mk
#endif

// Hide some C++ annotations that we'd like to use but need to avoid on older
// compilers.
#if __cplusplus <= 199711L
#define override
#endif

#define VSOC_PLATFORM_SDK_J                16
#define VSOC_PLATFORM_SDK_J_MR1            17
#define VSOC_PLATFORM_SDK_J_MR2            18
#define VSOC_PLATFORM_SDK_K                19
// Version 20 reserved for KitKat wearables only. See
// http://developer.android.com/guide/topics/manifest/uses-sdk-element.html
#define VSOC_PLATFORM_SDK_L                21
#define VSOC_PLATFORM_SDK_L_MR1            22
#define VSOC_PLATFORM_SDK_M                23
#define VSOC_PLATFORM_SDK_N                24
#define VSOC_PLATFORM_SDK_N_MR1            25
#define VSOC_PLATFORM_SDK_O                26
#define VSOC_PLATFORM_SDK_O_MR1            27
#define VSOC_PLATFORM_SDK_LAST_SHIPPED     27

#define VSOC_PLATFORM_SDK_BEFORE(X) (VSOC_PLATFORM_SDK_VERSION < VSOC_PLATFORM_SDK_##X)
#define VSOC_PLATFORM_SDK_AFTER(X) (VSOC_PLATFORM_SDK_VERSION > VSOC_PLATFORM_SDK_##X)

#if VSOC_PLATFORM_SDK_BEFORE(J_MR2)
#define VSOC_STATIC_INITIALIZER(X) X:
#else
#define VSOC_STATIC_INITIALIZER(X) .X =
#endif

#if VSOC_PLATFORM_SDK_BEFORE(K)
// audio_input_flags_t was first defind in K.
// JBMR2 and K use the same audio HAL version, so define a work-around here.
typedef enum {
  AUDIO_INPUT_FLAG_NONE       = 0x0,  // no attributes
} audio_input_flags_t;
#endif

// cstdint doesn't provide PRI... before C++11. On some branches (K) inttypes.h
// also doesn't behave as if it's C++. Just define the PRI... types here
// the kludgy way so our source is clean from a C++11 point-of-view.
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS 1
#endif
#include <inttypes.h>

#if VSOC_PLATFORM_SDK_BEFORE(L)
#define HAL_PIXEL_FORMAT_RAW16 HAL_PIXEL_FORMAT_RAW_SENSOR
#define VSOC_FDPRINTF fdprintf

#define KLOG_ERROR_LEVEL   3
#define KLOG_WARNING_LEVEL 4
#define KLOG_NOTICE_LEVEL  5
#define KLOG_INFO_LEVEL    6
#define KLOG_DEBUG_LEVEL   7

#else
#define VSOC_FDPRINTF dprintf
#endif

#if VSOC_PLATFORM_SDK_BEFORE(M)
__BEGIN_DECLS
extern int clock_nanosleep(clockid_t, int, const struct timespec*,
                             struct timespec*);
__END_DECLS
#endif
