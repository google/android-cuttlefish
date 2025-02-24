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

#include <ComposerClientWrapper.h>

#include <cstdlib>
#include <memory>

#include <log/log.h>
#include <signal.h>
#include <unistd.h>

#include "Readback.h"

/*
 * A simple binary that takes over the HWC through its AIDL Client Wrappers and
 * display a simple red color on the screen.
 */

namespace cuttlefish {

namespace {

using namespace ::aidl::android::hardware::graphics::composer3::
    libhwc_aidl_test;

class HwcTester {
 public:
  HwcTester() {
    mComposerClient = std::make_unique<ComposerClientWrapper>(
        IComposer::descriptor + std::string("/default"));
    if (!mComposerClient) {
      ALOGE("Failed to create HWC client");
    }

    if (!mComposerClient->createClient().isOk()) {
      ALOGE("Failed to create HWC client connection");
    }

    const auto& [status, displays] = mComposerClient->getDisplays();
    if (!status.isOk() || displays.empty()) {
      ALOGE("Failed to get displays");
      return;
    }
    mDisplays = displays;
  }

  ~HwcTester() {
    std::unordered_map<int64_t, ComposerClientWriter*> displayWriters;
    for (const auto& display : mDisplays) {
      displayWriters.emplace(display.getDisplayId(),
                             &getWriter(display.getDisplayId()));
    }
    mComposerClient->tearDown(displayWriters);
    mComposerClient.reset();
  }

  std::vector<DisplayConfiguration> GetDisplayConfigs(size_t diplay_idx) {
    const DisplayWrapper& display = mDisplays[diplay_idx];

    const auto& [configStatus, configs] =
        mComposerClient->getDisplayConfigurations(display.getDisplayId());
    if (!configStatus.isOk() || configs.empty()) {
      ALOGE("Failed to get display configs for display %zu)", diplay_idx);
    }

    return configs;
  }

  DisplayConfiguration getDisplayActiveConfigs(size_t diplay_idx) {
    const DisplayWrapper& display = mDisplays[diplay_idx];

    auto [activeConfigStatus, activeConfig] =
        mComposerClient->getActiveConfig(display.getDisplayId());
    if (!activeConfigStatus.isOk()) {
      ALOGE("Failed to get active config for display %zu", diplay_idx);
      return {};
    }

    DisplayConfiguration displayConfig;
    const auto& configs = GetDisplayConfigs(diplay_idx);
    for (const auto& config : configs) {
      if (config.configId == activeConfig) {
        return config;
      }
    }

    ALOGE("Active config was not found in configs for display %zu", diplay_idx);
    return {};
  }

  bool DrawSolidColorToScreen(size_t diplay_idx, Color color) {
    const DisplayWrapper& display = mDisplays[diplay_idx];

    // Create a layer for solid red color
    const auto& [status, layer] = mComposerClient->createLayer(
        display.getDisplayId(), kBufferSlotCount, nullptr);
    if (!status.isOk()) {
      ALOGE("Failed to create layer on display %zu", diplay_idx);
      return false;
    }

    // Create a writer for the display commands
    auto& writer = getWriter(display.getDisplayId());

    // Set layer properties
    writer.setLayerCompositionType(display.getDisplayId(), layer,
                                   Composition::SOLID_COLOR);
    writer.setLayerPlaneAlpha(display.getDisplayId(), layer, color.a);
    writer.setLayerColor(display.getDisplayId(), layer, color);

    DisplayConfiguration displayConfig = getDisplayActiveConfigs(diplay_idx);
    writer.setLayerDisplayFrame(
        display.getDisplayId(), layer,
        Rect{0, 0, displayConfig.width, displayConfig.height});
    writer.setLayerZOrder(display.getDisplayId(), layer, 0);

    // Validate and present display
    writer.validateDisplay(display.getDisplayId(),
                           ComposerClientWriter::kNoTimestamp, 0);
    writer.presentDisplay(display.getDisplayId());

    // Execute the commands
    auto commands = writer.takePendingCommands();
    std::pair<ScopedAStatus, std::vector<CommandResultPayload>> executeRes =
        mComposerClient->executeCommands(commands);
    return executeRes.first.isOk();
  }

 private:
  ComposerClientWriter& getWriter(int64_t display) {
    auto [it, _] = mWriters.try_emplace(display, display);
    return it->second;
  }

  std::unique_ptr<ComposerClientWrapper> mComposerClient;
  std::vector<DisplayWrapper> mDisplays;
  std::unordered_map<int64_t, ComposerClientWriter> mWriters;

  // use the slot count usually set by SF
  static constexpr uint32_t kBufferSlotCount = 64;
};

}  // namespace
}  // namespace cuttlefish

static volatile bool keep_running = true;
void signal_handler(int) { keep_running = false; }

int main() {
  cuttlefish::HwcTester tester;
  tester.DrawSolidColorToScreen(0, cuttlefish::RED);

  // Stay on, allowing the host tests to take screenshots and process.
  signal(SIGTERM, signal_handler);
  while (keep_running) {
    sleep(1);
  }

  return 0;
}
