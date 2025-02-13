#include "EdidInfo.h"

#include "Common.h"

namespace aidl::android::hardware::graphics::composer3::impl {
std::optional<EdidInfo> EdidInfo::parse(std::span<const uint8_t> blob) {
  constexpr size_t kEdidDescriptorOffset = 54;
  constexpr size_t kEdidDescriptorLength = 18;

  blob = blob.subspan(kEdidDescriptorOffset);

  using byte_view = std::span<const uint8_t>;
  byte_view descriptor(blob.data(), kEdidDescriptorLength);
  if (descriptor[0] == 0 && descriptor[1] == 0) {
    ALOGE("%s: missing preferred detailed timing descriptor", __FUNCTION__);
    return std::nullopt;
  }

  const uint8_t w_mm_lsb = descriptor[12];
  const uint8_t h_mm_lsb = descriptor[13];
  const uint8_t w_and_h_mm_msb = descriptor[14];

  return EdidInfo{
      .mWidthMillimeters =
          static_cast<uint32_t>(w_mm_lsb) |
          ((static_cast<uint32_t>(w_and_h_mm_msb) & 0xf0) << 4),
      .mHeightMillimeters =
          static_cast<uint32_t>(h_mm_lsb) |
          ((static_cast<uint32_t>(w_and_h_mm_msb) & 0xf) << 8),
  };
}
}  // namespace aidl::android::hardware::graphics::composer3::impl
