#ifndef DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_VNC_SERVER_VNC_UTILS_H_
#define DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_VNC_SERVER_VNC_UTILS_H_

#include <GceFrameBuffer.h>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#undef D
#ifdef GCE_VNC_DEBUG
#define D(...) ALOGD(__VA_ARGS__)
#else
#define D(...) ((void)0)
#endif

namespace avd {
namespace vnc {

// TODO(haining) when the hwcomposer gives a sequence number type, use that
// instead. It might just work to replace this class with a type alias
// using StripeSeqNumber = whatever_the_hwcomposer_uses;
class StripeSeqNumber {
 public:
  StripeSeqNumber() = default;
  explicit StripeSeqNumber(std::uint64_t t) : t_{t} {}
  bool operator<(const StripeSeqNumber& other) const { return t_ < other.t_; }

  bool operator<=(const StripeSeqNumber& other) const { return t_ <= other.t_; }

 private:
  std::uint64_t t_{};
};

using Message = std::vector<std::uint8_t>;

constexpr int32_t kJpegMaxQualityEncoding = -23;
constexpr int32_t kJpegMinQualityEncoding = -32;

enum class ScreenOrientation { Portrait, Landscape };
constexpr int kNumOrientations = 2;

struct Stripe {
  int index = -1;
  std::uint64_t frame_id{};
  std::uint16_t x{};
  std::uint16_t y{};
  std::uint16_t width{};
  std::uint16_t height{};
  Message raw_data{};
  Message jpeg_data{};
  StripeSeqNumber seq_number{};
  ScreenOrientation orientation{};
};

inline constexpr int BytesPerPixel() { return sizeof(GceFrameBuffer::Pixel); }

// The width of the screen regardless of orientation. Does not change.
inline int ActualScreenWidth() { return GceFrameBuffer::getInstance().x_res(); }

// The height of the screen regardless of orientation. Does not change.
inline int ActualScreenHeight() {
  return GceFrameBuffer::getInstance().y_res();
}

inline int ScreenSizeInBytes() {
  return ActualScreenWidth() * ActualScreenHeight() * BytesPerPixel();
}

}  // namespace vnc
}  // namespace avd

#endif
