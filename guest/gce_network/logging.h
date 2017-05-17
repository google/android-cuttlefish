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
#ifndef GCE_NETWORK_LOGGING_H_
#define GCE_NETWORK_LOGGING_H_

#ifndef LOG_TAG
#define LOG_TAG "GceInit"
#endif

extern "C" {
#include "cutils/klog.h"

// Fix for older cutils/klog.h file, which does not export the relevant value.
#ifndef KLOG_NOTICE_LEVEL
#define KLOG_ERROR_LEVEL   3
#define KLOG_WARNING_LEVEL 4
#define KLOG_NOTICE_LEVEL  5
#define KLOG_INFO_LEVEL    6
#define KLOG_DEBUG_LEVEL   7
#endif
}

#endif  // GCE_NETWORK_LOGGING_H_
