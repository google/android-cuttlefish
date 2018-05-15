#pragma once
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

#include <vector>

namespace vsoc {

class RegionMemoryLayout {
 public:
  RegionMemoryLayout(const char* region_name, size_t region_size,
                     int guest_to_host_signal_table_log_size,
                     int host_to_guest_signal_table_log_size,
                     const char* managed_by)
      : region_name_(region_name),
        region_size_(region_size),
        guest_to_host_signal_table_log_size_(
            guest_to_host_signal_table_log_size),
        host_to_guest_signal_table_log_size_(
            host_to_guest_signal_table_log_size),
        managed_by_(managed_by) {}

  const char* region_name() const { return region_name_; }
  size_t region_size() const { return region_size_; }
  int guest_to_host_signal_table_log_size() const {
    return guest_to_host_signal_table_log_size_;
  }
  int host_to_guest_signal_table_log_size() const {
    return host_to_guest_signal_table_log_size_;
  }
  const char * managed_by() const {
    return managed_by_;
  }

 private:
  const char* region_name_{};
  size_t region_size_{};
  int guest_to_host_signal_table_log_size_{};
  int host_to_guest_signal_table_log_size_{};
  const char* managed_by_{};
};

const std::vector<RegionMemoryLayout>& GetVsocMemoryLayout();

}  // namespace vsoc
