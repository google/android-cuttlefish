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

#include <string.h>
#include <unistd.h>

#include <map>
#include <string>
#include <type_traits>

#include "common/libs/glog/logging.h"
#include "common/vsoc/shm/audio_data_layout.h"
#include "common/vsoc/shm/base.h"
#include "common/vsoc/shm/e2e_test_region_layout.h"
#include "common/vsoc/shm/gralloc_layout.h"
#include "common/vsoc/shm/input_events_layout.h"
#include "common/vsoc/shm/managed_e2e_test_region_layout.h"
#include "common/vsoc/shm/ril_layout.h"
#include "common/vsoc/shm/screen_layout.h"
#include "common/vsoc/shm/socket_forward_layout.h"
#include "common/vsoc/shm/wifi_exchange_layout.h"

#include "uapi/vsoc_shm.h"

namespace {

// Takes a vector of objects and returns a vector of pointers to those objects.
template <typename T, typename R>
std::vector<R*> GetConstPointers(const std::vector<T>& v) {
  std::vector<R*> result;
  result.reserve(v.size());
  for (auto& element : v) {
    result.push_back(&element);
  }
  return result;
}
}  // namespace

namespace vsoc {

namespace {

class VSoCRegionLayoutImpl : public VSoCRegionLayout {
 public:
  VSoCRegionLayoutImpl(const char* region_name, size_t layout_size,
                       int guest_to_host_signal_table_log_size,
                       int host_to_guest_signal_table_log_size,
                       const char* managed_by)
      : region_name_(region_name),
        layout_size_(layout_size),
        guest_to_host_signal_table_log_size_(
            guest_to_host_signal_table_log_size),
        host_to_guest_signal_table_log_size_(
            host_to_guest_signal_table_log_size),
        managed_by_(managed_by) {
  }
  VSoCRegionLayoutImpl(const VSoCRegionLayoutImpl&) = default;

  const char* region_name() const override { return region_name_; }
  const char* managed_by() const override { return managed_by_; }

  size_t layout_size() const override { return layout_size_; }
  int guest_to_host_signal_table_log_size() const override {
    return guest_to_host_signal_table_log_size_;
  }
  int host_to_guest_signal_table_log_size() const override {
    return host_to_guest_signal_table_log_size_;
  }

 private:
  const char* region_name_{};
  const size_t layout_size_{};
  const int guest_to_host_signal_table_log_size_{};
  const int host_to_guest_signal_table_log_size_{};
  const char* managed_by_{};
};

class VSoCMemoryLayoutImpl : public VSoCMemoryLayout {
 public:
  explicit VSoCMemoryLayoutImpl(std::vector<VSoCRegionLayoutImpl>&& regions)
      : regions_(regions), region_idx_by_name_(GetNameToIndexMap(regions)) {
    for (size_t i = 0; i < regions_.size(); ++i) {
      // This link could be resolved later, but doing it here disables
      // managed_by cycles among the regions.
      if (regions[i].managed_by() &&
          !region_idx_by_name_.count(regions[i].managed_by())) {
        LOG(FATAL) << regions[i].region_name()
                   << " managed by unknown region: " << regions[i].managed_by()
                   << ". Manager Regions must be declared before the regions "
                      "they manage";
      }
    }
  }

  ~VSoCMemoryLayoutImpl() = default;

  std::vector<const VSoCRegionLayout*> GetRegions() const {
    static std::vector<const VSoCRegionLayout*> ret =
        GetConstPointers<VSoCRegionLayoutImpl, const VSoCRegionLayout>(
            regions_);
    return ret;
  }

  const VSoCRegionLayout* GetRegionByName(
      const char* region_name) const override {
    if (!region_idx_by_name_.count(region_name)) {
      return nullptr;
    }
    return &regions_[region_idx_by_name_.at(region_name)];
  }

 protected:
  VSoCMemoryLayoutImpl() = delete;
  VSoCMemoryLayoutImpl(const VSoCMemoryLayoutImpl&) = delete;

  // Helper function to allow the creation of the name to index map in the
  // constructor and allow the field to be const
  static std::map<const char*, size_t> GetNameToIndexMap(
      const std::vector<VSoCRegionLayoutImpl>& regions) {
    std::map<const char*, size_t> result;
    for (size_t index = 0; index < regions.size(); ++index) {
      auto region_name = regions[index].region_name();
      if (result.count(region_name)) {
        LOG(FATAL) << region_name << " used for more than one region";
      }
      result[region_name] = index;
    }
    return result;
  }

  std::vector<VSoCRegionLayoutImpl> regions_;
  const std::map<const char*, size_t> region_idx_by_name_;
};

template <class R>
VSoCRegionLayoutImpl ValidateAndBuildLayout(int g_to_h_signal_table_log_size,
                                            int h_to_g_signal_table_log_size,
                                            const char* managed_by = nullptr) {
  // Double check that the Layout is a valid shm type.
  ASSERT_SHM_COMPATIBLE(R);
  return VSoCRegionLayoutImpl(R::region_name, sizeof(R),
                              g_to_h_signal_table_log_size,
                              h_to_g_signal_table_log_size, managed_by);
}

}  // namespace

VSoCMemoryLayout* VSoCMemoryLayout::Get() {
  /*******************************************************************
   * Make sure the first region is not the manager of other regions. *
   *       This error will only be caught on runtime!!!!!            *
   *******************************************************************/
  static VSoCMemoryLayoutImpl layout(
      {ValidateAndBuildLayout<layout::input_events::InputEventsLayout>(2, 2),
       ValidateAndBuildLayout<layout::screen::ScreenLayout>(2, 2),
       ValidateAndBuildLayout<layout::gralloc::GrallocManagerLayout>(2, 2),
       ValidateAndBuildLayout<layout::gralloc::GrallocBufferLayout>(
           0, 0,
           /* managed_by */ layout::gralloc::GrallocManagerLayout::region_name),
       ValidateAndBuildLayout<layout::socket_forward::SocketForwardLayout>(7,
                                                                           7),
       ValidateAndBuildLayout<layout::wifi::WifiExchangeLayout>(2, 2),
       ValidateAndBuildLayout<layout::ril::RilLayout>(2, 2),
       ValidateAndBuildLayout<layout::e2e_test::E2EPrimaryTestRegionLayout>(1,
                                                                            1),
       ValidateAndBuildLayout<layout::e2e_test::E2ESecondaryTestRegionLayout>(
           1, 1),
       ValidateAndBuildLayout<layout::e2e_test::E2EManagerTestRegionLayout>(1,
                                                                            1),
       ValidateAndBuildLayout<layout::e2e_test::E2EManagedTestRegionLayout>(1,
                                                                            1),
       ValidateAndBuildLayout<layout::audio_data::AudioDataLayout>(2, 2)});

  // We need this code to compile on both sides to enforce the static checks,
  // but should only be used host side.
#if !defined(CUTTLEFISH_HOST)
  LOG(FATAL) << "Memory layout should not be used guest side, use region "
                "classes or the vsoc driver directly instead.";
#endif
  return &layout;
}

}  // namespace vsoc
