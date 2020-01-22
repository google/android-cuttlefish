/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <stdint.h>

#include <chrono>
#include <memory>
#include <vector>

#include <https/RunLoop.h>
#include <source/StreamingSource.h>

struct RTPSender;

struct Packetizer : public std::enable_shared_from_this<Packetizer> {

  using StreamingSource = android::StreamingSource;

  explicit Packetizer(std::shared_ptr<RunLoop> runLoop,
                      std::shared_ptr<StreamingSource> source);
  virtual ~Packetizer();

  virtual void run();
  virtual uint32_t rtpNow() const = 0;
  int32_t requestIDRFrame();

  virtual void queueRTPDatagram(std::vector<uint8_t> *packet);

  virtual void addSender(std::shared_ptr<RTPSender> sender);

  virtual void onFrame(const std::shared_ptr<android::SBuffer>& accessUnit);

 protected:
  virtual void packetize(const std::shared_ptr<android::SBuffer>& accessUnit,
                         int64_t timeUs) = 0;

  uint32_t timeSinceStart() const;

  int64_t mediaStartTime() const { return mStartTimeMedia; }

 private:
  size_t mNumSamplesRead;
  std::chrono::time_point<std::chrono::steady_clock> mStartTimeReal;
  int64_t mStartTimeMedia;

  std::shared_ptr<RunLoop> mRunLoop;

  std::shared_ptr<StreamingSource> mStreamingSource;

  std::vector<std::weak_ptr<RTPSender>> mSenders;
};
