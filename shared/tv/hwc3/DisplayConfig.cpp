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

#include "DisplayConfig.h"

#include <unordered_map>

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

template <class T>
inline void hashCombine(size_t& hash, const T& value) {
  std::hash<T> hasher;
  hash ^= hasher(value) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
}

}  // namespace

void DisplayConfig::setAttribute(DisplayAttribute attribute, int32_t value) {
  if (attribute == DisplayAttribute::WIDTH) {
    mWidth = value;
  }
  if (attribute == DisplayAttribute::HEIGHT) {
    mHeight = value;
  }
  if (attribute == DisplayAttribute::DPI_X) {
    mDpiX = value;
  }
  if (attribute == DisplayAttribute::DPI_Y) {
    mDpiY = value;
  }
  if (attribute == DisplayAttribute::VSYNC_PERIOD) {
    mVsyncPeriodNanos = value;
  }
  if (attribute == DisplayAttribute::CONFIG_GROUP) {
    mConfigGroup = value;
  }
}

int32_t DisplayConfig::getAttribute(DisplayAttribute attribute) const {
  if (attribute == DisplayAttribute::WIDTH) {
    return mWidth;
  }
  if (attribute == DisplayAttribute::HEIGHT) {
    return mHeight;
  }
  if (attribute == DisplayAttribute::DPI_X) {
    // From hwcomposer2.h, HWC2_ATTRIBUTE_DPI_X returns "Dots per thousand
    // inches (DPI * 1000)".
    return getDotsPerThousandInchesX();
  }
  if (attribute == DisplayAttribute::DPI_Y) {
    // From hwcomposer2.h, HWC2_ATTRIBUTE_DPI_Y returns "Dots per thousand
    // inches (DPI * 1000)"
    return getDotsPerThousandInchesY();
  }
  if (attribute == DisplayAttribute::VSYNC_PERIOD) {
    return mVsyncPeriodNanos;
  }
  if (attribute == DisplayAttribute::CONFIG_GROUP) {
    return mConfigGroup;
  }
  return -1;
}

std::string DisplayConfig::toString() const {
  std::string output;
  output += " id: " + std::to_string(mId);
  output += " w:" + std::to_string(mWidth);
  output += " h:" + std::to_string(mHeight);
  output += " dpi-x:" + std::to_string(mDpiX);
  output += " dpi-y:" + std::to_string(mDpiY);
  output += " vsync:" + std::to_string(1e9 / mVsyncPeriodNanos);
  output += " config-group:" + std::to_string(mConfigGroup);
  return output;
}

/*static*/
void DisplayConfig::addConfigGroups(std::vector<DisplayConfig>* configs) {
  // From /hardware/interfaces/graphics/composer/2.4/IComposerClient.hal:
  // "Configurations which share the same config group are similar in all
  // attributes except for the vsync period."
  struct ConfigForGroupHash {
    size_t operator()(const DisplayConfig& config) const {
      size_t hash = 0;
      hashCombine(hash, config.mWidth);
      hashCombine(hash, config.mHeight);
      hashCombine(hash, config.mDpiX);
      hashCombine(hash, config.mDpiY);
      return hash;
    }
  };
  struct ConfigForGroupEq {
    size_t operator()(const DisplayConfig& a, const DisplayConfig& b) const {
      if (a.mWidth != b.mWidth) {
        return a.mWidth < b.mWidth;
      }
      if (a.mHeight != b.mHeight) {
        return a.mHeight < b.mHeight;
      }
      if (a.mDpiX != b.mDpiX) {
        return a.mDpiX < b.mDpiX;
      }
      return a.mDpiY < b.mDpiY;
    }
  };

  std::unordered_map<DisplayConfig, int32_t, ConfigForGroupHash,
                     ConfigForGroupEq>
      configToConfigGroup;

  for (auto& config : *configs) {
    auto [it, inserted] =
        configToConfigGroup.try_emplace(config, configToConfigGroup.size());
    config.setConfigGroup(it->second);
  }
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
