#pragma once

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

#include "common/libs/fs/shared_fd.h"
#include "common/libs/threads/thread_annotations.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "common/libs/tcp_socket/tcp_socket.h"
#include "host/frontend/vnc_server/blackboard.h"
#include "host/frontend/vnc_server/virtual_inputs.h"
#include "host/frontend/vnc_server/vnc_utils.h"

namespace cvd {
namespace vnc {

class VncClientConnection {
 public:
  VncClientConnection(ClientSocket client,
                      std::shared_ptr<VirtualInputs> virtual_inputs,
                      BlackBoard* bb, bool aggressive);
  VncClientConnection(const VncClientConnection&) = delete;
  VncClientConnection& operator=(const VncClientConnection&) = delete;
  ~VncClientConnection();

  void StartSession();

 private:
  struct PixelFormat {
    std::uint8_t bits_per_pixel;
    std::uint8_t depth;
    std::uint8_t big_endian;
    std::uint8_t true_color;
    std::uint16_t red_max;
    std::uint16_t green_max;
    std::uint16_t blue_max;
    std::uint8_t red_shift;
    std::uint8_t green_shift;
    std::uint8_t blue_shift;
  };

  struct FrameBufferUpdateRequest {
    bool incremental;
    std::uint16_t x_pos;
    std::uint16_t y_pos;
    std::uint16_t width;
    std::uint16_t height;
  };

  friend bool operator==(const FrameBufferUpdateRequest&,
                         const FrameBufferUpdateRequest&);
  friend bool operator!=(const FrameBufferUpdateRequest&,
                         const FrameBufferUpdateRequest&);

  bool closed();
  void SetupProtocol();
  void SetupSecurityType();

  void GetClientInit();

  void SendServerInit() EXCLUDES(m_);
  static Message MakeFrameBufferUpdateHeader(std::uint16_t num_stripes);

  static void AppendRawStripeHeader(Message* frame_buffer_update,
                                    const Stripe& stripe);
  void AppendRawStripe(Message* frame_buffer_update, const Stripe& stripe) const
      REQUIRES(m_);
  Message MakeRawFrameBufferUpdate(const StripePtrVec& stripes) const
      REQUIRES(m_);

  static void AppendJpegSize(Message* frame_buffer_update, size_t jpeg_size);
  static void AppendJpegStripeHeader(Message* frame_buffer_update,
                                     const Stripe& stripe);
  static void AppendJpegStripe(Message* frame_buffer_update,
                               const Stripe& stripe);
  static Message MakeJpegFrameBufferUpdate(const StripePtrVec& stripes);

  Message MakeFrameBufferUpdate(const StripePtrVec& frame) REQUIRES(m_);

  void FrameBufferUpdateRequestHandler(bool aggressive) EXCLUDES(m_);

  void SendDesktopSizeUpdate() REQUIRES(m_);

  bool IsUrgent(const FrameBufferUpdateRequest& update_request) const;
  static StripeSeqNumber MostRecentStripeSeqNumber(const StripePtrVec& stripes);

  void HandleFramebufferUpdateRequest() EXCLUDES(m_);

  void HandleSetEncodings();

  void HandleSetPixelFormat();

  void HandlePointerEvent() EXCLUDES(m_);

  void UpdateAccelerometer(float x, float y, float z);

  struct Coordinates {
    float x;
    float y;
    float z;
  };

  Coordinates CoordinatesForOrientation(ScreenOrientation orientation) const;

  int ScreenWidth() const REQUIRES(m_);

  int ScreenHeight() const REQUIRES(m_);

  void SetScreenOrientation(ScreenOrientation orientation) EXCLUDES(m_);

  // Returns true if key is special and the screen was rotated.
  bool RotateIfIsRotationCommand(std::uint32_t key);

  void HandleKeyEvent();

  void HandleClientCutText();

  void NormalSession();

  mutable std::mutex m_;
  ClientSocket client_;
  bool control_key_down_ = false;
  bool meta_key_down_ = false;
  std::shared_ptr<VirtualInputs> virtual_inputs_{};

  FrameBufferUpdateRequest previous_update_request_{};
  BlackBoard* bb_;
  bool use_jpeg_compression_ GUARDED_BY(m_) = false;

  std::thread frame_buffer_request_handler_tid_;
  bool closed_ GUARDED_BY(m_){};

  PixelFormat pixel_format_ GUARDED_BY(m_) = {
      std::uint8_t{32},  // bits per pixel
      std::uint8_t{24},   // depth
      std::uint8_t{0},    // big_endian
      std::uint8_t{1},    // true_color
      std::uint16_t{0xff},   // red_max, (maxes not used when true color flag is 0)
      std::uint16_t{0xff},   // green_max
      std::uint16_t{0xff},   // blue_max
      std::uint8_t{0},  // red_shift (shifts not used when true color flag is 0)
      std::uint8_t{8},  // green_shift
      std::uint8_t{16},  // blue_shift
  };

  bool supports_desktop_size_encoding_ = false;
  ScreenOrientation current_orientation_ GUARDED_BY(m_) =
      ScreenOrientation::Portrait;

  bool client_is_old_ = false;
};

}  // namespace vnc
}  // namespace cvd
