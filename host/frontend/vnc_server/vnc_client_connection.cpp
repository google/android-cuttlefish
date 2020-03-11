/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "host/frontend/vnc_server/vnc_client_connection.h"

#include <netinet/in.h>
#include <sys/time.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include "common/libs/tcp_socket/tcp_socket.h"
#include "host/frontend/vnc_server/keysyms.h"
#include "host/frontend/vnc_server/mocks.h"
#include "host/frontend/vnc_server/vnc_utils.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/screen_connector/screen_connector.h"

using cvd::Message;
using cvd::vnc::Stripe;
using cvd::vnc::StripePtrVec;
using cvd::vnc::VncClientConnection;

struct ScreenRegionView {
  using Pixel = uint32_t;
  static constexpr int kSwiftShaderPadding = 4;
  static constexpr int kRedShift = 0;
  static constexpr int kGreenShift = 8;
  static constexpr int kBlueShift = 16;
  static constexpr int kRedBits = 8;
  static constexpr int kGreenBits = 8;
  static constexpr int kBlueBits = 8;
};

DEFINE_bool(debug_client, false, "Turn on detailed logging for the client");

#define DLOG(LEVEL) \
  if (FLAGS_debug_client) LOG(LEVEL)

namespace {
class BigEndianChecker {
 public:
  BigEndianChecker() {
    uint32_t u = 1;
    is_big_endian_ = *reinterpret_cast<const char*>(&u) == 0;
  }
  bool operator()() const { return is_big_endian_; }

 private:
  bool is_big_endian_{};
};

const BigEndianChecker ImBigEndian;

constexpr int32_t kDesktopSizeEncoding = -223;
constexpr int32_t kTightEncoding = 7;

// These are the lengths not counting the first byte. The first byte
// indicates the message type.
constexpr size_t kSetPixelFormatLength = 19;
constexpr size_t kFramebufferUpdateRequestLength = 9;
constexpr size_t kSetEncodingsLength = 3;  // more bytes follow
constexpr size_t kKeyEventLength = 7;
constexpr size_t kPointerEventLength = 5;
constexpr size_t kClientCutTextLength = 7;  // more bytes follow

std::string HostName() {
  auto config = vsoc::CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();
  return !config || instance.device_title().empty() ? std::string{"localhost"}
                                                    : instance.device_title();
}

std::uint16_t uint16_tAt(const void* p) {
  std::uint16_t u{};
  std::memcpy(&u, p, sizeof u);
  return ntohs(u);
}

std::uint32_t uint32_tAt(const void* p) {
  std::uint32_t u{};
  std::memcpy(&u, p, sizeof u);
  return ntohl(u);
}

std::int32_t int32_tAt(const void* p) {
  std::uint32_t u{};
  std::memcpy(&u, p, sizeof u);
  u = ntohl(u);
  std::int32_t s{};
  std::memcpy(&s, &u, sizeof s);
  return s;
}

std::uint32_t RedVal(std::uint32_t pixel) {
  return (pixel >> ScreenRegionView::kRedShift) &
         ((0x1 << ScreenRegionView::kRedBits) - 1);
}

std::uint32_t BlueVal(std::uint32_t pixel) {
  return (pixel >> ScreenRegionView::kBlueShift) &
         ((0x1 << ScreenRegionView::kBlueBits) - 1);
}

std::uint32_t GreenVal(std::uint32_t pixel) {
  return (pixel >> ScreenRegionView::kGreenShift) &
         ((0x1 << ScreenRegionView::kGreenBits) - 1);
}
}  // namespace
namespace cvd {
namespace vnc {
bool operator==(const VncClientConnection::FrameBufferUpdateRequest& lhs,
                const VncClientConnection::FrameBufferUpdateRequest& rhs) {
  return lhs.x_pos == rhs.x_pos && lhs.y_pos == rhs.y_pos &&
         lhs.width == rhs.width && lhs.height == rhs.height;
}

bool operator!=(const VncClientConnection::FrameBufferUpdateRequest& lhs,
                const VncClientConnection::FrameBufferUpdateRequest& rhs) {
  return !(lhs == rhs);
}
}  // namespace vnc
}  // namespace cvd

VncClientConnection::VncClientConnection(
    ClientSocket client, std::shared_ptr<VirtualInputs> virtual_inputs,
    BlackBoard* bb, bool aggressive)
    : client_{std::move(client)}, virtual_inputs_{virtual_inputs}, bb_{bb} {
  frame_buffer_request_handler_tid_ = std::thread(
      &VncClientConnection::FrameBufferUpdateRequestHandler, this, aggressive);
}

VncClientConnection::~VncClientConnection() {
  {
    std::lock_guard<std::mutex> guard(m_);
    closed_ = true;
  }
  bb_->StopWaiting(this);
  frame_buffer_request_handler_tid_.join();
}

void VncClientConnection::StartSession() {
  LOG(INFO) << "Starting session";
  SetupProtocol();
  LOG(INFO) << "Protocol set up";
  if (client_.closed()) {
    return;
  }
  SetupSecurityType();
  LOG(INFO) << "Security type set";
  if (client_.closed()) {
    return;
  }
  GetClientInit();
  LOG(INFO) << "Gotten client init";
  if (client_.closed()) {
    return;
  }
  SendServerInit();
  LOG(INFO) << "Sent server init";
  if (client_.closed()) {
    return;
  }
  NormalSession();
  LOG(INFO) << "vnc session terminated";
}

bool VncClientConnection::closed() {
  std::lock_guard<std::mutex> guard(m_);
  return closed_;
}

void VncClientConnection::SetupProtocol() {
  static constexpr char kRFBVersion[] = "RFB 003.008\n";
  static constexpr char kRFBVersionOld[] = "RFB 003.003\n";
  static constexpr auto kVersionLen = (sizeof kRFBVersion) - 1;
  client_.SendNoSignal(reinterpret_cast<const std::uint8_t*>(kRFBVersion),
                       kVersionLen);
  auto client_protocol = client_.Recv(kVersionLen);
  if (std::memcmp(&client_protocol[0], kRFBVersion,
                  std::min(kVersionLen, client_protocol.size())) != 0) {
    if (!std::memcmp(
                &client_protocol[0],
                kRFBVersionOld,
                std::min(kVersionLen, client_protocol.size()))) {
        // We'll deal with V3.3 as well.
        client_is_old_ = true;
        return;
    }

    client_protocol.push_back('\0');
    LOG(ERROR) << "vnc client wants a different protocol: "
               << reinterpret_cast<const char*>(&client_protocol[0]);
  }
}

void VncClientConnection::SetupSecurityType() {
  if (client_is_old_) {
    static constexpr std::uint8_t kVNCSecurity[4] = { 0x00, 0x00, 0x00, 0x02 };
    client_.SendNoSignal(kVNCSecurity);

    static constexpr std::uint8_t kChallenge[16] =
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    client_.SendNoSignal(kChallenge);

    auto clientResponse = client_.Recv(16);
    (void)clientResponse;  // Accept any response, we're not interested in actual security.

    static constexpr std::uint8_t kSuccess[4] = { 0x00, 0x00, 0x00, 0x00 };
    client_.SendNoSignal(kSuccess);
    return;
  }

  static constexpr std::uint8_t kNoneSecurity = 0x1;
  // The first '0x1' indicates the number of items that follow
  static constexpr std::uint8_t kOnlyNoneSecurity[] = {0x01, kNoneSecurity};
  client_.SendNoSignal(kOnlyNoneSecurity);
  auto client_security = client_.Recv(1);
  if (client_.closed()) {
    return;
  }
  if (client_security.front() != kNoneSecurity) {
    LOG(ERROR) << "vnc client is asking for security type "
               << static_cast<int>(client_security.front());
  }
  static constexpr std::uint8_t kZero[4] = {};
  client_.SendNoSignal(kZero);
}

void VncClientConnection::GetClientInit() {
  auto client_shared = client_.Recv(1);
}

void VncClientConnection::SendServerInit() {
  const std::string server_name = HostName();
  std::lock_guard<std::mutex> guard(m_);
  auto server_init = cvd::CreateMessage(
      static_cast<std::uint16_t>(ScreenWidth()),
      static_cast<std::uint16_t>(ScreenHeight()), pixel_format_.bits_per_pixel,
      pixel_format_.depth, pixel_format_.big_endian, pixel_format_.true_color,
      pixel_format_.red_max, pixel_format_.green_max, pixel_format_.blue_max,
      pixel_format_.red_shift, pixel_format_.green_shift,
      pixel_format_.blue_shift, std::uint16_t{},  // padding
      std::uint8_t{},                             // padding
      static_cast<std::uint32_t>(server_name.size()), server_name);
  client_.SendNoSignal(server_init);
}

Message VncClientConnection::MakeFrameBufferUpdateHeader(
    std::uint16_t num_stripes) {
  return cvd::CreateMessage(std::uint8_t{0},  // message-type
                            std::uint8_t{},   // padding
                            std::uint16_t{num_stripes});
}

void VncClientConnection::AppendRawStripeHeader(Message* frame_buffer_update,
                                                const Stripe& stripe) {
  static constexpr int32_t kRawEncoding = 0;
  cvd::AppendToMessage(frame_buffer_update, std::uint16_t{stripe.x},
                       std::uint16_t{stripe.y}, std::uint16_t{stripe.width},
                       std::uint16_t{stripe.height}, kRawEncoding);
}

void VncClientConnection::AppendJpegSize(Message* frame_buffer_update,
                                         size_t jpeg_size) {
  constexpr size_t kJpegSizeOneByteMax = 127;
  constexpr size_t kJpegSizeTwoByteMax = 16383;
  constexpr size_t kJpegSizeThreeByteMax = 4194303;

  if (jpeg_size <= kJpegSizeOneByteMax) {
    cvd::AppendToMessage(frame_buffer_update,
                         static_cast<std::uint8_t>(jpeg_size));
  } else if (jpeg_size <= kJpegSizeTwoByteMax) {
    auto sz = static_cast<std::uint32_t>(jpeg_size);
    cvd::AppendToMessage(frame_buffer_update,
                         static_cast<std::uint8_t>((sz & 0x7F) | 0x80),
                         static_cast<std::uint8_t>((sz >> 7) & 0xFF));
  } else {
    if (jpeg_size > kJpegSizeThreeByteMax) {
      LOG(FATAL) << "jpeg size is too big: " << jpeg_size << " must be under "
                 << kJpegSizeThreeByteMax;
    }
    const auto sz = static_cast<std::uint32_t>(jpeg_size);
    cvd::AppendToMessage(frame_buffer_update,
                         static_cast<std::uint8_t>((sz & 0x7F) | 0x80),
                         static_cast<std::uint8_t>(((sz >> 7) & 0x7F) | 0x80),
                         static_cast<std::uint8_t>((sz >> 14) & 0xFF));
  }
}

void VncClientConnection::AppendRawStripe(Message* frame_buffer_update,
                                          const Stripe& stripe) const {
  using Pixel = ScreenRegionView::Pixel;
  auto& fbu = *frame_buffer_update;
  AppendRawStripeHeader(&fbu, stripe);
  auto init_size = fbu.size();
  fbu.insert(fbu.end(), stripe.raw_data.begin(), stripe.raw_data.end());
  for (size_t i = init_size; i < fbu.size(); i += sizeof(Pixel)) {
    CHECK_LE(i + sizeof(Pixel), fbu.size());
    Pixel raw_pixel{};
    std::memcpy(&raw_pixel, &fbu[i], sizeof raw_pixel);
    auto red = RedVal(raw_pixel);
    auto green = GreenVal(raw_pixel);
    auto blue = BlueVal(raw_pixel);
    Pixel pixel = Pixel{red} << pixel_format_.red_shift |
                  Pixel{blue} << pixel_format_.blue_shift |
                  Pixel{green} << pixel_format_.green_shift;

    if (bool(pixel_format_.big_endian) != ImBigEndian()) {
      // flip them bits (refactor into function)
      auto p = reinterpret_cast<char*>(&pixel);
      std::swap(p[0], p[3]);
      std::swap(p[1], p[2]);
    }
    std::memcpy(&fbu[i], &pixel, sizeof pixel);
  }
}

Message VncClientConnection::MakeRawFrameBufferUpdate(
    const StripePtrVec& stripes) const {
  auto fbu =
      MakeFrameBufferUpdateHeader(static_cast<std::uint16_t>(stripes.size()));
  for (auto& stripe : stripes) {
    AppendRawStripe(&fbu, *stripe);
  }
  return fbu;
}

void VncClientConnection::AppendJpegStripeHeader(Message* frame_buffer_update,
                                                 const Stripe& stripe) {
  static constexpr std::uint8_t kJpegEncoding = 0x90;
  cvd::AppendToMessage(frame_buffer_update, stripe.x, stripe.y, stripe.width,
                       stripe.height, kTightEncoding, kJpegEncoding);
  AppendJpegSize(frame_buffer_update, stripe.jpeg_data.size());
}

void VncClientConnection::AppendJpegStripe(Message* frame_buffer_update,
                                           const Stripe& stripe) {
  AppendJpegStripeHeader(frame_buffer_update, stripe);
  frame_buffer_update->insert(frame_buffer_update->end(),
                              stripe.jpeg_data.begin(), stripe.jpeg_data.end());
}

Message VncClientConnection::MakeJpegFrameBufferUpdate(
    const StripePtrVec& stripes) {
  auto fbu =
      MakeFrameBufferUpdateHeader(static_cast<std::uint16_t>(stripes.size()));
  for (auto& stripe : stripes) {
    AppendJpegStripe(&fbu, *stripe);
  }
  return fbu;
}

Message VncClientConnection::MakeFrameBufferUpdate(
    const StripePtrVec& stripes) {
  return use_jpeg_compression_ ? MakeJpegFrameBufferUpdate(stripes)
                               : MakeRawFrameBufferUpdate(stripes);
}

void VncClientConnection::FrameBufferUpdateRequestHandler(bool aggressive) {
  BlackBoard::Registerer reg(bb_, this);

  while (!closed()) {
    auto stripes = bb_->WaitForSenderWork(this);
    if (closed()) {
      break;
    }
    if (stripes.empty()) {
      LOG(FATAL) << "Got 0 stripes";
    }
    {
      // lock here so a portrait frame can't be sent after a landscape
      // DesktopSize update, or vice versa.
      std::lock_guard<std::mutex> guard(m_);
      DLOG(INFO) << "Sending update in "
                 << (current_orientation_ == ScreenOrientation::Portrait
                         ? "portrait"
                         : "landscape")
                 << " mode";
      client_.SendNoSignal(MakeFrameBufferUpdate(stripes));
    }
    if (aggressive) {
      bb_->FrameBufferUpdateRequestReceived(this);
    }
  }
}

void VncClientConnection::SendDesktopSizeUpdate() {
  static constexpr int32_t kDesktopSizeEncoding = -223;
  client_.SendNoSignal(cvd::CreateMessage(
      std::uint8_t{0},   // message-type,
      std::uint8_t{},    // padding
      std::uint16_t{1},  // one pseudo rectangle
      std::uint16_t{0}, std::uint16_t{0},
      static_cast<std::uint16_t>(ScreenWidth()),
      static_cast<std::uint16_t>(ScreenHeight()), kDesktopSizeEncoding));
}

bool VncClientConnection::IsUrgent(
    const FrameBufferUpdateRequest& update_request) const {
  return !update_request.incremental ||
         update_request != previous_update_request_;
}

void VncClientConnection::HandleFramebufferUpdateRequest() {
  auto msg = client_.Recv(kFramebufferUpdateRequestLength);
  if (msg.size() != kFramebufferUpdateRequestLength) {
    return;
  }
  FrameBufferUpdateRequest fbur{msg[1] == 0, uint16_tAt(&msg[1]),
                                uint16_tAt(&msg[3]), uint16_tAt(&msg[5]),
                                uint16_tAt(&msg[7])};
  if (IsUrgent(fbur)) {
    bb_->SignalClientNeedsEntireScreen(this);
  }
  bb_->FrameBufferUpdateRequestReceived(this);
  previous_update_request_ = fbur;
}

void VncClientConnection::HandleSetEncodings() {
  auto msg = client_.Recv(kSetEncodingsLength);
  if (msg.size() != kSetEncodingsLength) {
    return;
  }
  auto count = uint16_tAt(&msg[1]);
  auto encodings = client_.Recv(count * sizeof(int32_t));
  if (encodings.size() % sizeof(int32_t) != 0) {
    return;
  }
  {
    std::lock_guard<std::mutex> guard(m_);
    use_jpeg_compression_ = false;
  }
  for (size_t i = 0; i < encodings.size(); i += sizeof(int32_t)) {
    auto enc = int32_tAt(&encodings[i]);
    DLOG(INFO) << "client requesting encoding: " << enc;
    if (enc == kTightEncoding) {
      // This is a deviation from the spec which says that if a jpeg quality
      // level is not specified, tight encoding won't use jpeg.
      std::lock_guard<std::mutex> guard(m_);
      use_jpeg_compression_ = true;
    }
    if (kJpegMinQualityEncoding <= enc && enc <= kJpegMaxQualityEncoding) {
      DLOG(INFO) << "jpeg compression level: " << enc;
      bb_->set_jpeg_quality_level(enc);
    }
    if (enc == kDesktopSizeEncoding) {
      supports_desktop_size_encoding_ = true;
    }
  }
}

void VncClientConnection::HandleSetPixelFormat() {
  std::lock_guard<std::mutex> guard(m_);
  auto msg = client_.Recv(kSetPixelFormatLength);
  if (msg.size() != kSetPixelFormatLength) {
    return;
  }
  pixel_format_.bits_per_pixel = msg[3];
  pixel_format_.depth = msg[4];
  pixel_format_.big_endian = msg[5];
  pixel_format_.true_color = msg[7];
  pixel_format_.red_max = uint16_tAt(&msg[8]);
  pixel_format_.green_max = uint16_tAt(&msg[10]);
  pixel_format_.blue_max = uint16_tAt(&msg[12]);
  pixel_format_.red_shift = msg[13];
  pixel_format_.green_shift = msg[14];
  pixel_format_.blue_shift = msg[15];
}

void VncClientConnection::HandlePointerEvent() {
  auto msg = client_.Recv(kPointerEventLength);
  if (msg.size() != kPointerEventLength) {
    return;
  }
  std::uint8_t button_mask = msg[0];
  auto x_pos = uint16_tAt(&msg[1]);
  auto y_pos = uint16_tAt(&msg[3]);
  {
    std::lock_guard<std::mutex> guard(m_);
    if (current_orientation_ == ScreenOrientation::Landscape) {
      std::tie(x_pos, y_pos) =
          std::make_pair(ScreenConnector::ScreenWidth() - y_pos, x_pos);
    }
  }
  virtual_inputs_->HandlePointerEvent(button_mask, x_pos, y_pos);
}

void VncClientConnection::UpdateAccelerometer(float /*x*/, float /*y*/,
                                              float /*z*/) {
  // TODO(jemoreira): Implement when vsoc sensor hal is updated
}

VncClientConnection::Coordinates VncClientConnection::CoordinatesForOrientation(
    ScreenOrientation orientation) const {
  // Compute the acceleration vector that we need to send to mimic
  // this change.
  constexpr float g = 9.81;
  constexpr float angle = 20.0;
  const float cos_angle = std::cos(angle / M_PI);
  const float sin_angle = std::sin(angle / M_PI);
  const float z = g * sin_angle;
  switch (orientation) {
    case ScreenOrientation::Portrait:
      return {0, g * cos_angle, z};
    case ScreenOrientation::Landscape:
      return {g * cos_angle, 0, z};
  }
}

int VncClientConnection::ScreenWidth() const {
  return current_orientation_ == ScreenOrientation::Portrait
             ? ScreenConnector::ScreenWidth()
             : ScreenConnector::ScreenHeight();
}

int VncClientConnection::ScreenHeight() const {
  return current_orientation_ == ScreenOrientation::Portrait
             ? ScreenConnector::ScreenHeight()
             : ScreenConnector::ScreenWidth();
}

void VncClientConnection::SetScreenOrientation(ScreenOrientation orientation) {
  std::lock_guard<std::mutex> guard(m_);
  auto coords = CoordinatesForOrientation(orientation);
  UpdateAccelerometer(coords.x, coords.y, coords.z);
  if (supports_desktop_size_encoding_) {
    auto previous_orientation = current_orientation_;
    current_orientation_ = orientation;
    if (current_orientation_ != previous_orientation &&
        supports_desktop_size_encoding_) {
      SendDesktopSizeUpdate();
      bb_->SetOrientation(this, current_orientation_);
      // TODO not sure if I should be sending a frame update along with this,
      // or just letting the next FBUR handle it. This seems to me like it's
      // sending one more frame buffer update than was requested, which is
      // maybe a violation of the spec?
    }
  }
}

bool VncClientConnection::RotateIfIsRotationCommand(std::uint32_t key) {
  // Due to different configurations on different platforms we're supporting
  // a set of options for rotating the screen. These are similar to what
  // the emulator supports and has supported.
  // ctrl+left and ctrl+right work on windows and linux
  // command+left and command+right work on Mac
  // ctrl+fn+F11 and ctrl+fn+F12 work when chromoting to ubuntu from a Mac
  if (!control_key_down_ && !meta_key_down_) {
    return false;
  }
  switch (key) {
    case cvd::xk::Right:
    case cvd::xk::F12:
      DLOG(INFO) << "switching to portrait";
      SetScreenOrientation(ScreenOrientation::Portrait);
      break;
    case cvd::xk::Left:
    case cvd::xk::F11:
      DLOG(INFO) << "switching to landscape";
      SetScreenOrientation(ScreenOrientation::Landscape);
      break;
    default:
      return false;
  }
  return true;
}

void VncClientConnection::HandleKeyEvent() {
  auto msg = client_.Recv(kKeyEventLength);
  if (msg.size() != kKeyEventLength) {
    return;
  }

  auto key = uint32_tAt(&msg[3]);
  bool key_down = msg[0];
  switch (key) {
    case cvd::xk::ControlLeft:
    case cvd::xk::ControlRight:
      control_key_down_ = key_down;
      break;
    case cvd::xk::MetaLeft:
    case cvd::xk::MetaRight:
      meta_key_down_ = key_down;
      break;
    case cvd::xk::F5:
      key = cvd::xk::Menu;
      break;
    case cvd::xk::F7:
      virtual_inputs_->PressPowerButton(key_down);
      return;
    default:
      break;
  }

  if (RotateIfIsRotationCommand(key)) {
    return;
  }

  virtual_inputs_->GenerateKeyPressEvent(key, key_down);
}

void VncClientConnection::HandleClientCutText() {
  auto msg = client_.Recv(kClientCutTextLength);
  if (msg.size() != kClientCutTextLength) {
    return;
  }
  auto len = uint32_tAt(&msg[3]);
  client_.Recv(len);
}

void VncClientConnection::NormalSession() {
  static constexpr std::uint8_t kSetPixelFormatMessage{0};
  static constexpr std::uint8_t kSetEncodingsMessage{2};
  static constexpr std::uint8_t kFramebufferUpdateRequestMessage{3};
  static constexpr std::uint8_t kKeyEventMessage{4};
  static constexpr std::uint8_t kPointerEventMessage{5};
  static constexpr std::uint8_t kClientCutTextMessage{6};
  while (true) {
    if (client_.closed()) {
      return;
    }
    auto msg = client_.Recv(1);
    if (client_.closed()) {
      return;
    }
    auto msg_type = msg.front();
    DLOG(INFO) << "Received message type " << msg_type;

    switch (msg_type) {
      case kSetPixelFormatMessage:
        HandleSetPixelFormat();
        break;

      case kSetEncodingsMessage:
        HandleSetEncodings();
        break;

      case kFramebufferUpdateRequestMessage:
        HandleFramebufferUpdateRequest();
        break;

      case kKeyEventMessage:
        HandleKeyEvent();
        break;

      case kPointerEventMessage:
        HandlePointerEvent();
        break;

      case kClientCutTextMessage:
        HandleClientCutText();
        break;

      default:
        LOG(WARNING) << "message type not handled: "
                     << static_cast<int>(msg_type);
        break;
    }
  }
}
