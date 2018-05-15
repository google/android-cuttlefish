/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "common/vsoc/lib/vsoc_memory.h"

#include <string>
#include <type_traits>

#include "common/vsoc/shm/audio_data_layout.h"
#include "common/vsoc/shm/e2e_test_region_layout.h"
#include "common/vsoc/shm/gralloc_layout.h"
#include "common/vsoc/shm/input_events_layout.h"
#include "common/vsoc/shm/ril_layout.h"
#include "common/vsoc/shm/screen_layout.h"
#include "common/vsoc/shm/socket_forward_layout.h"
#include "common/vsoc/shm/version.h"
#include "common/vsoc/shm/wifi_exchange_layout.h"

namespace vsoc {

// ShmTypeValidator provides meaningful information about the type size
// mismatch in compilation error messages, eg.
//
// error:
//    static_assert failed "Class size changed, update version.h"
//    static_assert(Current == Expected,
// note: in instantiation of template class
//    'ShmTypeValidator<vsoc::layout::myclass::ClassName, 1232, 1240>'
//    requested here ASSERT_SHM_COMPATIBLE(ClassName, myclass);
//
template <typename Type, size_t Current, size_t Expected>
struct ShmTypeValidator {
  static_assert(Current == Expected, "Class size changed, update version.h");
  static_assert(std::is_trivial<Type>(), "Class uses features that are unsafe");
  static constexpr bool valid =
      (Current == Expected) && std::is_trivial<Type>();
};

namespace {
template <class R>
RegionMemoryLayout ValidateAndBuildLayout(int region_size,
                                          int g_to_h_signal_table_log_size,
                                          int h_to_g_signal_table_log_size,
                                          const char* managed_by = nullptr) {
  static_assert(
      ShmTypeValidator<R, sizeof(R), vsoc::layout::VersionInfo<R>::size>::valid,
      "Compilation error. Please fix above errors and retry.");
  return RegionMemoryLayout(R::region_name, region_size,
                            g_to_h_signal_table_log_size,
                            h_to_g_signal_table_log_size, managed_by);
}

}  // namespace

const std::vector<RegionMemoryLayout>& GetVsocMemoryLayout() {
  static const std::vector<RegionMemoryLayout> layout = {
      ValidateAndBuildLayout<layout::input_events::InputEventsLayout>(
          /*size*/ 4096, /*g->h*/ 2, /*h->g*/ 2),
      ValidateAndBuildLayout<layout::screen::ScreenLayout>(
          /*size*/ 12292096, /*g->h*/ 2, /*h->g*/ 2),
      ValidateAndBuildLayout<layout::gralloc::GrallocManagerLayout>(
          /*size*/ 40960, /*g->h*/ 2, /*h->g*/ 2),
      ValidateAndBuildLayout<layout::gralloc::GrallocBufferLayout>(
          /*size*/ 407142400, /*g->h*/ 0, /*h->g*/ 0,
          /* managed_by */ layout::gralloc::GrallocManagerLayout::region_name),
      ValidateAndBuildLayout<layout::socket_forward::SocketForwardLayout>(
          /*size*/ 2105344, /*g->h*/ 7, /*h->g*/ 7),
      ValidateAndBuildLayout<layout::wifi::WifiExchangeLayout>(
          /*size*/ 139264, /*g->h*/ 2, /*h->g*/ 2),
      ValidateAndBuildLayout<layout::ril::RilLayout>(
          /*size*/ 4096, /*g->h*/ 2, /*h->g*/ 2),
      ValidateAndBuildLayout<layout::e2e_test::E2EPrimaryTestRegionLayout>(
          /*size*/ 16384, /*g->h*/ 1, /*h->g*/ 1),
      ValidateAndBuildLayout<layout::e2e_test::E2ESecondaryTestRegionLayout>(
          /*size*/ 16384, /*g->h*/ 1, /*h->g*/ 1),
      ValidateAndBuildLayout<layout::e2e_test::E2EManagerTestRegionLayout>(
          /*size*/ 4096, /*g->h*/ 1, /*h->g*/ 1),
      ValidateAndBuildLayout<layout::e2e_test::E2EManagedTestRegionLayout>(
          /*size*/ 16384, /*g->h*/ 1, /*h->g*/ 1),
      ValidateAndBuildLayout<layout::audio_data::AudioDataLayout>(
          /*size*/ 20480, /*g->h*/ 2, /*h->g*/ 2)};

  return layout;
}

}  // namespace vsoc
