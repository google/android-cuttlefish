/*
 * Copyright (C) 2024 The Android Open Source Project
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

#pragma once

#include <linux/videodev2.h>
#include <iostream>
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "utils/Timers.h"
#include "vsock_connection.h"

namespace cuttlefish {

// VsockFrameSource accepts WebRTC YUV camera stream data
// over vsock, converts it to v4l2 format BGRX32, and then
// writes the result to a v4l2 device.  This allows for creation
// of v4l2 devices in guest VMs, and streaming to them
// from Cuttlefish's WebRTC UI via any connected camera.
class VsockFrameSource {
 public:
  // Starts a Frame Source streaming session targeting a
  // specific v4l2 device
  static Result<std::unique_ptr<VsockFrameSource>> Start(
      const std::string& v4l2_device_path);

  ~VsockFrameSource();

  // Stops a thread managing the stream if running, and closes the v4l2 device.
  void Stop();

  // Returns true if there is a camera stream currently running
  bool Running();

  // This is a blocking method, that runs while connection is valid.
  // It receives frames from a vsock socket, formats the data stream and
  // sends it to a v4l2 output device.
  void VsockReadLoop();

  // Starts a Thread which invokes VsockReadLoop(). This allows the calling
  // thread to perform other operations while this frame source is sending data.
  Result<void> VsockReadLoopThreaded();

 private:
  // The v4l2 device path to receive camera frames, ie /dev/video0
  std::string v4l2_device_path_;
  std::unique_ptr<cuttlefish::VsockConnection> connection_;
  std::thread reader_thread_;
  std::atomic<bool> running_;
  std::mutex frame_mutex_;
  std::mutex settings_mutex_;
  std::atomic<nsecs_t> timestamp_;
  std::condition_variable yuv_frame_updated_;

  // File handle of v4l2 device to be written to
  SharedFD fd_v4l2_device_;

  // Following frame_* values will be set after successful connection.
  // Host process sends a message which conveys the camera dimensions
  // to this guest instance over the vsock connection.
  int frame_width_ = 0;
  int frame_height_ = 0;
  int frame_rate_ = 0;
  int frame_size_ = 0;

  // Currently this class only supports writing to v4l2 devices
  // via this format.
  int format_ = V4L2_PIX_FMT_BGRX32;

  // Verifies that given data is a video frame. Used to
  // distinguish control messages.
  bool FramesizeMatches(const std::vector<char>& data);

  // Determines if a vsock packet contains special data
  // that is not camera frame.
  bool IsBlob(const std::vector<char>& blob);

  // Sends message to Host process communicating an event in the
  // camera connection state. ie - when to start or stop streaming.
  bool WriteJsonEventMessage(const std::string& message);

  // After connect, this is called to retrieve camera dimensions
  // and properties needed to initialize the v4l2 device and allocate
  // buffers necessary for streaming.
  Result<bool> ReadSettingsFromJson(const Json::Value& json);

  // Established the vsock connection
  bool Connect();

  // Called once every frame to write a frame buffer to the v4l2
  // output device.
  void WriteFrame(const std::vector<char>& frame, std::vector<char>& rgb_frame);

 protected:
  VsockFrameSource() = default;
};

}  // End namespace cuttlefish