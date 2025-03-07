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

#include "hwc_tester.h"
#include <inttypes.h>
#include <log/log.h>

namespace cuttlefish {

namespace {
static constexpr uint32_t kBufferSlotCount = 64;

}  // namespace

HwcTester::HwcTester() {
  mComposerClient = std::make_unique<libhwc_aidl_test::ComposerClientWrapper>(
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

  for (const auto& display : displays) {
    mDisplays.emplace(display.getDisplayId(), std::move(display));
  }
}

HwcTester::~HwcTester() {
  std::unordered_map<int64_t, ComposerClientWriter*> displayWriters;
  for (const auto& [id, _] : mDisplays) {
    displayWriters.emplace(id, &GetWriter(id));
  }
  mComposerClient->tearDown(displayWriters);
  mComposerClient.reset();
}

std::vector<int64_t> HwcTester::GetAllDisplayIds() const {
  std::vector<int64_t> displayIds(mDisplays.size());

  for (const auto& [id, _] : mDisplays) {
    displayIds.push_back(id);
  }

  return displayIds;
}

std::vector<DisplayConfiguration> HwcTester::GetDisplayConfigs(
    int64_t displayId) {
  const auto& [configStatus, configs] =
      mComposerClient->getDisplayConfigurations(displayId);
  if (!configStatus.isOk() || configs.empty()) {
    ALOGE("Failed to get display configs for display %" PRId64, displayId);
  }

  return configs;
}

DisplayConfiguration HwcTester::GetDisplayActiveConfigs(int64_t displayId) {
  auto [activeConfigStatus, activeConfig] =
      mComposerClient->getActiveConfig(displayId);
  if (!activeConfigStatus.isOk()) {
    ALOGE("Failed to get active config for display %" PRId64, displayId);
    return {};
  }

  DisplayConfiguration displayConfig;
  const auto& configs = GetDisplayConfigs(displayId);
  for (const auto& config : configs) {
    if (config.configId == activeConfig) {
      return config;
    }
  }

  ALOGE("Active config was not found in configs for display %" PRId64,
        displayId);
  return {};
}

bool HwcTester::DrawSolidColorToScreen(int64_t displayId, Color color) {
  // Create a layer for solid color
  const auto& [status, layer] =
      mComposerClient->createLayer(displayId, kBufferSlotCount, nullptr);
  if (!status.isOk()) {
    ALOGE("Failed to create layer on display %" PRId64, displayId);
    return false;
  }

  // Create a writer for the display commands
  auto& writer = GetWriter(displayId);

  // Set layer properties
  writer.setLayerCompositionType(displayId, layer, Composition::SOLID_COLOR);
  writer.setLayerPlaneAlpha(displayId, layer, color.a);
  writer.setLayerColor(displayId, layer, color);

  DisplayConfiguration displayConfig = GetDisplayActiveConfigs(displayId);
  writer.setLayerDisplayFrame(
      displayId, layer, Rect{0, 0, displayConfig.width, displayConfig.height});
  writer.setLayerZOrder(displayId, layer, 0);

  // Validate and present display
  writer.validateDisplay(displayId, ComposerClientWriter::kNoTimestamp, 0);
  writer.presentDisplay(displayId);

  // Execute the commands
  auto commands = writer.takePendingCommands();
  std::pair<ScopedAStatus, std::vector<CommandResultPayload>> executeRes =
      mComposerClient->executeCommands(commands);
  return executeRes.first.isOk();
}

ComposerClientWriter& HwcTester::GetWriter(int64_t display) {
  auto [it, _] = mWriters.try_emplace(display, display);
  return it->second;
}

}  // namespace cuttlefish
