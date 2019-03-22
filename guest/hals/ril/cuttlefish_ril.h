/*
 * Copyright (C) 2006 The Android Open Source Project
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

#define RIL_SHLIB

#define LOG_TAG "CuttlefishRil"

#include <log/log.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>

// Start to use and maintain the local ril.h file from Q.
#include "guest/libs/platform_support/api_level_fixes.h"
#if VSOC_PLATFORM_SDK_BEFORE(Q)
#include <telephony/ril.h>
#else
#include <guest/hals/ril/libril/ril.h>
#endif

#include <telephony/ril_cdma_sms.h>