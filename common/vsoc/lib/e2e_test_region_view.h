#pragma once
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

#include "common/vsoc/lib/typed_region_view.h"
#include "common/vsoc/shm/e2e_test_region.h"

namespace vsoc {
template <typename Layout>
class E2ERegionView : public vsoc::TypedRegionView<Layout> {
 public:
  const char* guest_string(size_t index) const {
    return make_nonvolatile(this->data().data[index].guest_writable);
  }

  const char* host_string(size_t index) const {
    return make_nonvolatile(this->data().data[index].host_writable);
  }

  bool set_guest_string(size_t index, const char* value) {
    strcpy(make_nonvolatile(this->data()->data[index].guest_writable), value);
    return true;
  }

  bool set_host_string(size_t index, const char* value) {
    strcpy(make_nonvolatile(this->data()->data[index].host_writable), value);
    return true;
  }

  size_t string_size() const {
    return Layout::NumFillRecords(this->region_data_size());
  }

  void guest_status(vsoc::layout::e2e_test::E2ETestStage stage) {
    this->data()->guest_status.set_value(stage);
  }

  void host_status(vsoc::layout::e2e_test::E2ETestStage stage) {
    this->data()->host_status.set_value(stage);
  }

  bool HasIncomingInterruptFromPeer() { return this->HasIncomingInterrupt(); }

  void SendInterruptToPeer() { this->InterruptPeer(); }

  void WaitForInterruptFromPeer() { this->WaitForInterrupt(); }

 protected:
  /**
   * The string functions have problems with volatile pointers, so
   * this function casts them away.
   */
  template <typename T>
  static T* make_nonvolatile(volatile T* in) {
    return (T*)in;
  }
};

using E2EPrimaryRegionView =
    vsoc::E2ERegionView<layout::e2e_test::E2EPrimaryTestRegionLayout>;
using E2ESecondaryRegionView =
    vsoc::E2ERegionView<layout::e2e_test::E2ESecondaryTestRegionLayout>;
using E2EUnfindableRegionView =
    vsoc::E2ERegionView<layout::e2e_test::E2EUnfindableRegionLayout>;
};  // namespace vsoc
