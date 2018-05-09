#pragma once

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

// Version information for structures that are present in VSoC shared memory
// windows. The proper use of this file will:
//
//   * ensure that the guest and host builds agree on the sizes of the shared
//     structures.
//
//   * provides a single version code for the entire vsoc layout, assuming
//     that reviewers excercise some care.
//
//
//  Use:
//
//    Every new class / structure in the shm folder needs to add a size
//    entry here, #include the base.h file, and add a ASSERT_SHM_COMPATIBLE
//    instantiation just below the class definition,
//
//    For templatized classes / structs the author should choose a fixed size,
//    create a using alias, and instantiate the checks on the alias.
//    See CircularByteQueue64k for an example of this usage.
//
//   Note to reviewers:
//
//     It is probably ok to approve new additions  here without forcing a
//     a version change.
//
//     However, the version must increment for any change in the value of a
//     constant.
//
//     #ifdef, etc is absolutely forbidden in this file and highly discouraged
//     in the other vsoc/shm files.

#include <cstdint>

namespace vsoc {
namespace layout {
namespace version_info {
namespace multi_region {

namespace {
// Increment this for any change in size in the fundamental structures listed
// below.  If the size of any of these structures changes we must examine all
// of the regions bumping the version number if they used the fields in their
// definition.
const uint32_t version = 0;
}  // namespace

static const std::size_t Base_size = 1;
static const std::size_t CircularQueueBase64k_size = 65548;
static const std::size_t CircularByteQueue64k_size = 65548;
static const std::size_t CircularPacketQueue64k_size = 65548;
static const std::size_t GuestLock_size = 40;
static const std::size_t GuestAndHostLock_size = 40;
static const std::size_t HostLock_size = 40;
static const std::size_t PixelFormatRegister_size = 4;
static const std::size_t PixelFormatMaskRegister_size = 8;
static const std::size_t RegionLayout_size = 1;
static const std::size_t Sides_size = 4;
static const std::size_t SpinLock_size = 4;
static const std::size_t WaitingLockBase_size = 40;

namespace constant_values {
static const uint32_t VSOC_PIXEL_FORMAT_UNINITIALIZED = 0;
static const uint32_t VSOC_PIXEL_FORMAT_BLOB = 1;

static const uint32_t VSOC_PIXEL_FORMAT_RGB_565 = 8;
static const uint32_t VSOC_PIXEL_FORMAT_YV12 = 9;
static const uint32_t VSOC_PIXEL_FORMAT_YCbCr_420_888 = 10;

static const uint32_t VSOC_PIXEL_FORMAT_RGB_888 = 16;

static const uint32_t VSOC_PIXEL_FORMAT_RGBA_8888 = 24;
static const uint32_t VSOC_PIXEL_FORMAT_RGBX_8888 = 25;
static const uint32_t VSOC_PIXEL_FORMAT_BGRA_8888 = 26;
static const uint32_t VSOC_PIXEL_FORMAT_RGBA_FP16 = 56;
}  // namespace constant_values
}  // namespace multi_region

// Versioning information for gralloc_layout.h
// Changes to these structures will affect only the gralloc region
namespace gralloc {
namespace {
const uint32_t version = 0;
}
static const std::size_t BufferEntry_size = 32;
static const std::size_t GrallocManagerLayout_size = 80;
static const std::size_t GrallocBufferLayout_size = 1;
}  // namespace gralloc

// Versioning information for input_events_layout.h
// Changes to these structures will affect only the input_events region
namespace input_events {
namespace {
const uint32_t version = 0;
}
// Three circular queues, each with a 1024 bytes buffer, a 32 bits spinlock and
// two 32 bits integers.
static const std::size_t InputEventsLayout_size = 3 * (1024 + 3 * 4);
}  // namespace input_events

// Versioning information for screen_layout.h
// Changes to these structures will affect only the screen region.
namespace screen {
namespace {
const uint32_t version = 0;
}
static const std::size_t ScreenLayout_size = 112;
}  // namespace screen

// Versioning information for wifi_layout.h
namespace wifi {
namespace {
constexpr uint32_t version = 1;
}  // namespace
constexpr size_t WifiExchangeLayout_size =
    65548 + // sizeof(CircularPacketQueue<16, 8192>) - forward
    65548 + // sizeof(CircularPacketQueue<16, 8192>) - reverse
    6 +     // uint8_t[6] MAC address.
    6;     // uint8_t[6] MAC address.
}  // namespace wifi

// Versioning information for ril_layout.h
namespace ril {
namespace {
const uint32_t version = 1;
}
static const std::size_t RilLayout_size = 68;
}  // namespace ril

// Versioning Information for socket_forward_layout.h
namespace socket_forward {
constexpr uint32_t version = 0;
constexpr std::size_t kNumQueues = 16;
constexpr std::size_t SocketForwardLayout_size =
    ((((65548  + 4) // queue + state
       * 2) // host_to_guest and guest_to_host
      + 4 + 4) // port and state_lock
     * kNumQueues)
    + 4 // seq_num
    + 4; // generation number
}  // namespace socket_forward

// Versioning information for e2e_test_region.h
// Changes to these structures will affect only the e2e_test_region
namespace e2e_test {
namespace {
const uint32_t version = 2;
}
static const std::size_t E2EManagerTestRegionLayout_size = 16;
static const std::size_t E2EMemoryFill_size = 64;
static const std::size_t E2EPrimaryTestRegionLayout_size = 84;
static const std::size_t E2ESecondaryTestRegionLayout_size = 84;
static const std::size_t E2ETestRegionLayout_size = 84;
static const std::size_t E2ETestStageRegister_size = 4;
static const std::size_t E2EUnfindableRegionLayout_size = 84;
static const std::size_t E2EManagedTestRegionLayout_size = 4;
}  // namespace e2e_test

// Versioning information for audio_data_layout.h
// Changes to these structures will affect only the audio_data region
namespace audio_data {
namespace {
const uint32_t version = 0;
}
// One circular queue of with a 16KB buffer, a 32 bits spinlock and
// two 32 bits integers.
static const std::size_t AudioDataLayout_size = 16384 + 3 * 4;
}  // namespace audio_data

}  // namespace version_info
}  // namespace layout
}  // namespace vsoc
