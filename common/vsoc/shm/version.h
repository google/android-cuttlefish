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

#include "common/vsoc/shm/audio_data_layout.h"
#include "common/vsoc/shm/e2e_test_region_layout.h"
#include "common/vsoc/shm/gralloc_layout.h"
#include "common/vsoc/shm/input_events_layout.h"
#include "common/vsoc/shm/ril_layout.h"
#include "common/vsoc/shm/screen_layout.h"
#include "common/vsoc/shm/socket_forward_layout.h"
#include "common/vsoc/shm/wifi_exchange_layout.h"

namespace vsoc {
namespace layout {

template <typename T>
class VersionInfo {
 public:
  constexpr static size_t size = -1;
};

// Versioning information for audio_data_layout.h
// Changes to these structures will affect only the audio_data region
template <>
class VersionInfo<audio_data::AudioDataLayout> {
 public:
  // One circular queue of with a 16KB buffer, a 32 bits spinlock and
  // two 32 bits integers.
  constexpr static size_t size = 16384 + 3 * 4;
};

// Versioning information for e2e_test_region.h
// Changes to these structures will affect only the e2e_test_region
template <>
class VersionInfo<e2e_test::E2EManagerTestRegionLayout> {
 public:
  constexpr static size_t size = 16;
};
template <>
class VersionInfo<e2e_test::E2EPrimaryTestRegionLayout> {
 public:
  constexpr static size_t size = 84;
};
template <>
class VersionInfo<e2e_test::E2ESecondaryTestRegionLayout> {
 public:
  constexpr static size_t size = 84;
};
template <>
class VersionInfo<e2e_test::E2ETestRegionLayout> {
 public:
  constexpr static size_t size = 84;
};
template <>
class VersionInfo<e2e_test::E2EUnfindableRegionLayout> {
 public:
  constexpr static size_t size = 84;
};
template <>
class VersionInfo<e2e_test::E2EManagedTestRegionLayout> {
 public:
  constexpr static size_t size = 4;
};

// Versioning information for gralloc_layout.h
// Changes to these structures will affect only the gralloc region
template <>
class VersionInfo<gralloc::GrallocManagerLayout> {
 public:
  constexpr static size_t size = 80;
};
template <>
class VersionInfo<gralloc::GrallocBufferLayout> {
 public:
  constexpr static size_t size = 1;
};

// Versioning information for input_events_layout.h
// Changes to these structures will affect only the input_events region
template <>
class VersionInfo<input_events::InputEventsLayout> {
 public:
  // Three circular queues, each with a 1024 bytes buffer, a 32 bits spinlock
  // and two 32 bits integers.
  constexpr static size_t size = 3 * (1024 + 3 * 4);
};

// Versioning information for ril_layout.h
template <>
class VersionInfo<ril::RilLayout> {
 public:
  constexpr static size_t size = 68;
};

// Versioning information for screen_layout.h
// Changes to these structures will affect only the screen region.
template <>
class VersionInfo<screen::ScreenLayout> {
 public:
  constexpr static size_t size = 112;
};

// Versioning Information for socket_forward_layout.h
template <>
class VersionInfo<socket_forward::SocketForwardLayout> {
 public:
  constexpr static size_t size = ((((65548 + 4)  // queue + state
                                    * 2)     // host_to_guest and guest_to_host
                                   + 4 + 4)  // port and state_lock
                                  * socket_forward::kNumQueues) +
                                 4     // seq_num
                                 + 4;  // generation number
};

// Versioning information for wifi_layout.h
template <>
class VersionInfo<wifi::WifiExchangeLayout> {
 public:
  constexpr static size_t size =
      65548 +  // sizeof(CircularPacketQueue<16, 8192>) - forward
      65548 +  // sizeof(CircularPacketQueue<16, 8192>) - reverse
      6 +      // uint8_t[6] MAC address.
      6;       // uint8_t[6] MAC address.
};

}  // namespace layout
}  // namespace vsoc
