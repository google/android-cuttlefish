/*
 * Copyright 2022 The Android Open Source Project
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

#ifndef ANDROID_HWC_DISPLAYCONFIG_H
#define ANDROID_HWC_DISPLAYCONFIG_H

#include <aidl/android/hardware/graphics/composer3/DisplayAttribute.h>

#include <vector>

#include "Common.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class DisplayConfig {
 public:
  DisplayConfig(int32_t configId) : mId(configId) {}

  DisplayConfig(int32_t configId, int32_t width, int32_t height, int32_t dpiX,
                int32_t dpiY, int32_t vsyncPeriodNanos)
      : mId(configId),
        mWidth(width),
        mHeight(height),
        mDpiX(dpiX),
        mDpiY(dpiY),
        mVsyncPeriodNanos(vsyncPeriodNanos) {}

  DisplayConfig(const DisplayConfig& other) = default;
  DisplayConfig& operator=(DisplayConfig& other) = default;

  DisplayConfig(DisplayConfig&& other) = default;
  DisplayConfig& operator=(DisplayConfig&& other) = default;

  int32_t getId() const { return mId; }
  void setId(int32_t id) { mId = id; }

  int32_t getAttribute(DisplayAttribute attribute) const;
  void setAttribute(DisplayAttribute attribute, int32_t value);

  int32_t getWidth() const { return mWidth; }
  void setWidth(int32_t width) { mWidth = width; }

  int32_t getHeight() const { return mHeight; }
  void getHeight(int32_t height) { mHeight = height; }

  int32_t getDpiX() const { return mDpiX; }
  void setDpiX(int32_t dpi) { mDpiX = dpi; }

  int32_t getDpiY() const { return mDpiY; }
  void setDpiY(int32_t dpi) { mDpiY = dpi; }

  int32_t getDotsPerThousandInchesX() const { return mDpiX * 1000; }
  int32_t getDotsPerThousandInchesY() const { return mDpiY * 1000; }

  int32_t getVsyncPeriod() const { return mVsyncPeriodNanos; }
  void setVsyncPeriod(int32_t vsync) { mVsyncPeriodNanos = vsync; }

  int32_t getConfigGroup() const { return mConfigGroup; }
  void setConfigGroup(int32_t group) { mConfigGroup = group; }

  std::string toString() const;

  static void addConfigGroups(std::vector<DisplayConfig>* configs);

 private:
  int32_t mId;
  int32_t mWidth;
  int32_t mHeight;
  int32_t mDpiX;
  int32_t mDpiY;
  int32_t mVsyncPeriodNanos;
  int32_t mConfigGroup;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif