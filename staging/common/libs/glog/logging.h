#pragma once
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

#ifdef ANDROID
#include <android-base/logging.h>

#if defined(CUTTLEFISH_HOST)
using ::android::base::VERBOSE;
using ::android::base::INFO;
using ::android::base::WARNING;
using ::android::base::FATAL;

#define LOG_IF(LEVEL, CONDITION) if (CONDITION) LOG(LEVEL)

#endif  // CUTTLEFISH_HOST
#else  // DEBIAN_HOST (by elimination)
#include <glog/logging.h>
#endif
