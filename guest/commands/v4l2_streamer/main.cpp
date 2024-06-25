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

#include <stdio.h>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "guest/commands/v4l2_streamer/vsock_frame_source.h"

DEFINE_bool(service_mode, false,
            "true to log output to Logd, false for stderr");

int main(int argc, char **argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_service_mode) {
    ::android::base::InitLogging(
        argv, android::base::LogdLogger(android::base::SYSTEM));
  } else {
    ::android::base::InitLogging(argv, android::base::StderrLogger);
  }

  android::base::SetDefaultTag("cuttlefish_v4l2_streamer");

  LOG(INFO) << "streamer starting...  ";

  auto vfs = cuttlefish::VsockFrameSource::Start("/dev/video0");

  if (vfs.ok()) {
    LOG(INFO) << "streamer initialized, streaming in progress...";

    vfs->get()->VsockReadLoop();

    LOG(INFO) << "streamer terminated.";
  } else {
    LOG(FATAL) << "start failed.";
  }
}