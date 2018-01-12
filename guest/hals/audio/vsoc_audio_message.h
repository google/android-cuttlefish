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
#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <system/audio.h>

struct gce_audio_message {
  static const char* kAudioHALSocketName;
  static const size_t kMaxAudioFrameLen = 65536;
  enum message_t {
    UNKNOWN = 0,
    DATA_SAMPLES = 1,
    OPEN_INPUT_STREAM = 2,
    OPEN_OUTPUT_STREAM = 3,
    CLOSE_INPUT_STREAM = 4,
    CLOSE_OUTPUT_STREAM = 5,
    CONTROL_PAUSE = 100
  };
  // Size of the header + data. Used to frame when we're on TCP.
  size_t total_size;
  // Size of the audio header
  size_t header_size;
  message_t message_type;
  // Identifier for the stream.
  uint32_t stream_number;
  // HAL assigned frame number, starts from 0.
  int64_t frame_num;
  // MONOTONIC_TIME when these frames were presented to the HAL.
  timespec time_presented;
  // Sample rate from the audio configuration.
  uint32_t frame_rate;
  // Channel mask from the audio configuration.
  audio_channel_mask_t channel_mask;
  // Format from the audio configuration.
  audio_format_t format;
  // Size of each frame in bytes.
  size_t frame_size;
  // Number of frames that were presented to the HAL.
  size_t num_frames_presented;
  // Number of frames that the HAL accepted.
  //   For blocking audio this will be the same as num_frames.
  //   For non-blocking audio this may be less.
  size_t num_frames_accepted;
  // Count of the number of packets that were dropped because they would
  // have blocked the HAL or exceeded the maximum message size.
  size_t num_packets_dropped;
  // Count of the number of packets that were shortened to fit within
  // kMaxAudioFrameLen.
  size_t num_packets_shortened;
  // num_frames_presented (not num_frames_accepted) will follow here.

  gce_audio_message() :
      total_size(sizeof(gce_audio_message)),
      header_size(sizeof(gce_audio_message)),
      message_type(UNKNOWN),
      stream_number(0),
      frame_num(0),
      frame_rate(0),
      channel_mask(0),
      format(AUDIO_FORMAT_DEFAULT),
      frame_size(0),
      num_frames_presented(0),
      num_frames_accepted(0),
      num_packets_dropped(0),
      num_packets_shortened(0) {
    time_presented.tv_sec = 0;
    time_presented.tv_nsec = 0;
  }
};
