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

#include <stdint.h>

#include <vector>

namespace vsoc {

class VSoCRegionLayout {
 public:
  virtual const char* region_name() const = 0;
  virtual const char* managed_by() const = 0;

  virtual size_t layout_size() const = 0;
  virtual int guest_to_host_signal_table_log_size() const = 0;
  virtual int host_to_guest_signal_table_log_size() const = 0;
 protected:
  VSoCRegionLayout() = default;
  virtual ~VSoCRegionLayout() = default;
};

class VSoCMemoryLayout {
 public:
  // Returns a pointer to the memory layout singleton.
  static VSoCMemoryLayout* Get();

  VSoCMemoryLayout(const VSoCMemoryLayout&) = delete;
  VSoCMemoryLayout(VSoCMemoryLayout&&) = delete;

  virtual std::vector<const VSoCRegionLayout *> GetRegions() const = 0;
  virtual const VSoCRegionLayout* GetRegionByName(
      const char* region_name) const = 0;
 protected:
  VSoCMemoryLayout() = default;
  virtual ~VSoCMemoryLayout() = default;
};

}  // namespace vsoc
