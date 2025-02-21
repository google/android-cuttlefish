#pragma once

#include <cinttypes>
#include <optional>
#include <span>

namespace aidl::android::hardware::graphics::composer3::impl {

struct EdidInfo {
  uint32_t mWidthMillimeters = 0;
  uint32_t mHeightMillimeters = 0;

  static std::optional<EdidInfo> parse(std::span<const uint8_t> blob);
};

}  // namespace aidl::android::hardware::graphics::composer3::impl
