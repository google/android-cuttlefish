/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <sys/socket.h>  // IWYU pragma: keep

#ifdef __linux__
// Must be included after <sys/socket.h> to support older libc.
#include <linux/vm_sockets.h>  // IWYU pragma: export
#elif defined(__APPLE__)
#include <sys/vsock.h>  // IWYU pragma: export
#endif

#ifndef VMADDR_CID_ANY
#define VMADDR_CID_ANY (-1U)
#endif
#ifndef VMADDR_PORT_ANY
#define VMADDR_PORT_ANY (-1U)
#endif
#ifndef VMADDR_CID_HYPERVISOR
#define VMADDR_CID_HYPERVISOR 0U
#endif
#ifndef VMADDR_CID_LOCAL
#define VMADDR_CID_LOCAL 1U
#endif
#ifndef VMADDR_CID_HOST
#define VMADDR_CID_HOST 2U
#endif
