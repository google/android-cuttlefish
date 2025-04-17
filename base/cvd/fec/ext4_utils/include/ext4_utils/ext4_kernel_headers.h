/*
 * Copyright (C) 2014 The Android Open Source Project
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

#ifndef _EXT4_UTILS_EXT4_KERNEL_HEADERS_H_
#define _EXT4_UTILS_EXT4_KERNEL_HEADERS_H_

#include <stdint.h>

#ifdef __BIONIC__
#include <sys/types.h>
#else
#define __le64 uint64_t
#define __le32 uint32_t
#define __le16 uint16_t

#define __be64 uint64_t
#define __be32 uint32_t
#define __be16 uint16_t

#define __u64 uint64_t
#define __u32 uint32_t
#define __u16 uint16_t
#define __u8 uint8_t
#endif

#include "ext4_utils/ext4.h"
#include "ext4_utils/ext4_extents.h"
#include "ext4_utils/jbd2.h"
#include "ext4_utils/xattr.h"

#ifndef __BIONIC__
#undef __le64
#undef __le32
#undef __le16

#undef __be64
#undef __be32
#undef __be16

#undef __u64
#undef __u32
#undef __u16
#undef __u8
#endif

#endif
