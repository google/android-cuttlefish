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
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "common/vsoc/lib/screen_region_view.h"
#include "guest/hals/hwcomposer/common/screen_view.h"

namespace cvd {

class VSoCScreenView : public ScreenView {
 public:
  VSoCScreenView();
  virtual ~VSoCScreenView();

  void Broadcast(int buffer_id,
                 const CompositionStats* stats = nullptr) override;
  void* GetBuffer(int fb_index) override;

  int32_t x_res() const override;
  int32_t y_res() const override;
  int32_t dpi() const override;
  int32_t refresh_rate() const override;

  int num_buffers() const override;

 private:
  vsoc::screen::ScreenRegionView* region_view_;
};

}  // namespace cvd
