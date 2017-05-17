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
#ifndef DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_NETWORK_JB_COMPAT_H_
#define DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_NETWORK_JB_COMPAT_H_

#include "api_level_fixes.h"

#if GCE_PLATFORM_SDK_BEFORE(J_MR1)
#include <linux/posix_types.h>
#include <linux/socket.h>
#include <linux/uio.h>

#define sockaddr __kernel_sockaddr_storage
#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_INET 2
#define AF_NETLINK 16
#endif

#endif  // DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_NETWORK_JB_COMPAT_H_
