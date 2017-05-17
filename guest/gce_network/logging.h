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
#ifndef GUEST_GCE_NETWORK_LOGGING_H_
#define GUEST_GCE_NETWORK_LOGGING_H_

#ifndef LOG_TAG
#define LOG_TAG "GceInit"
#endif

#include <stdio.h>
#define KLOG_DEBUG(tag, fmt...) printf("D: " tag ": " fmt)
#define KLOG_INFO(tag, fmt...) printf("I: " tag ": " fmt)
#define KLOG_WARNING(tag, fmt...) printf("W: " tag ": " fmt)
#define KLOG_ERROR(tag, fmt...) printf("E: " tag ": " fmt)

#endif  // GUEST_GCE_NETWORK_LOGGING_H_
