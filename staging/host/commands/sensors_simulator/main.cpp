/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <string>
#include <vector>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "common/libs/transport/channel_sharedfd.h"
#include "host/commands/sensors_simulator/sensors_simulator.h"
#include "host/libs/config/logging.h"

DEFINE_int32(sensors_in_fd, -1, "Sensors virtio-console from host to guest");
DEFINE_int32(sensors_out_fd, -1, "Sensors virtio-console from guest to host");
DEFINE_int32(webrtc_fd, -1, "A file descriptor to communicate with webrtc");

namespace cuttlefish {
namespace sensors {

namespace {

static constexpr char kReqMisFormatted[] = "The request is mis-formatted.";

Result<void> ProcessWebrtcRequest(transport::SharedFdChannel& channel,
                                  SensorsSimulator& sensors_simulator) {
  auto request =
      CF_EXPECT(channel.ReceiveMessage(), "Couldn't receive message.");
  std::stringstream ss(std::string(
      reinterpret_cast<const char*>(request->payload), request->payload_size));
  SensorsCmd cmd = request->command;
  switch (cmd) {
    case kUpdateRotationVec: {
      double x, y, z;
      char delimiter;
      CF_EXPECT((ss >> x >> delimiter) && (delimiter == INNER_DELIM),
                kReqMisFormatted);
      CF_EXPECT((ss >> y >> delimiter) && (delimiter == INNER_DELIM),
                kReqMisFormatted);
      CF_EXPECT(static_cast<bool>(ss >> z), kReqMisFormatted);
      sensors_simulator.RefreshSensors(x, y, z);
      break;
    }
    case kGetSensorsData: {
      int mask;
      CF_EXPECT(static_cast<bool>(ss >> mask), kReqMisFormatted);
      auto sensors_data = sensors_simulator.GetSensorsData(mask);
      auto size = sensors_data.size();
      cmd = kGetSensorsData;
      auto response =
          CF_EXPECT(transport::CreateMessage(cmd, true, size),
                    "Failed to allocate message for cmd: "
                        << cmd << " with size: " << size << " bytes.");
      memcpy(response->payload, sensors_data.data(), size);
      CF_EXPECT(channel.SendResponse(*response),
                "Can't send request for cmd: " << cmd);
      break;
    }
    default: {
      return CF_ERR("Unsupported cmd: " << cmd);
    }
  }
  return {};
}

int SensorsSimulatorMain(int argc, char** argv) {
  DefaultSubprocessLogging(argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  auto webrtc_fd = SharedFD::Dup(FLAGS_webrtc_fd);
  close(FLAGS_webrtc_fd);
  if (!webrtc_fd->IsOpen()) {
    LOG(FATAL) << "Unable to connect webrtc: " << webrtc_fd->StrError();
  }
  transport::SharedFdChannel channel(webrtc_fd, webrtc_fd);
  SensorsSimulator sensors_simulator;
  while (true) {
    auto result = ProcessWebrtcRequest(channel, sensors_simulator);
    if (!result.ok()) {
      LOG(ERROR) << result.error().FormatForEnv();
    }
  }
  return 0;
}

}  // namespace

}  // namespace sensors
}  // namespace cuttlefish

int main(int argc, char* argv[]) {
  return cuttlefish::sensors::SensorsSimulatorMain(argc, argv);
}