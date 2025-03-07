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

#include <log/log.h>
#include "Readback.h"
#include "src/utils/hwc_tester.h"

/*
 * A simple binary that takes over the HWC through its AIDL Client Wrappers and
 * display a simple red color on the screen.
 */

static volatile bool keep_running = true;
void signal_handler(int) { keep_running = false; }

int main() {
  cuttlefish::HwcTester tester;

  // Get all available displays
  auto display_ids = tester.GetAllDisplayIds();
  if (display_ids.empty()) {
    ALOGE("No displays available");
    return 1;
  }

  tester.DrawSolidColorToScreen(display_ids[0], libhwc_aidl_test::RED);

  // Stay on, allowing the host tests to take screenshots and process.
  signal(SIGTERM, signal_handler);
  while (keep_running) {
    sleep(1);
  }

  return 0;
}
