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

#include <ComposerClientWrapper.h>

#include <unistd.h>
#include <memory>
#include <unordered_map>
#include <vector>

namespace cuttlefish {

namespace libhwc_aidl_test =
    ::aidl::android::hardware::graphics::composer3::libhwc_aidl_test;

/*
 * HwcTester is a class that provides an interface to interact with the
 * HWC AIDL through libhwc_aidl_test. It's not just an interface to the HWC
 * AIDL, but also provides some helper functions to make it easier to write
 * tests.
 */

class HwcTester {
 public:
  HwcTester();
  ~HwcTester();

  // Returns a list of all display IDs
  std::vector<int64_t> GetAllDisplayIds() const;

  // Draw a solid color to the specified display using its ID
  bool DrawSolidColorToScreen(int64_t display_id, Color color);

 private:
  std::vector<DisplayConfiguration> GetDisplayConfigs(int64_t display_id);
  DisplayConfiguration GetDisplayActiveConfigs(int64_t display_id);
  ComposerClientWriter& GetWriter(int64_t display_id);

  std::unique_ptr<libhwc_aidl_test::ComposerClientWrapper> mComposerClient;
  std::unordered_map<int64_t, libhwc_aidl_test::DisplayWrapper> mDisplays;
  std::unordered_map<int64_t, ComposerClientWriter> mWriters;
};

}  // namespace cuttlefish
