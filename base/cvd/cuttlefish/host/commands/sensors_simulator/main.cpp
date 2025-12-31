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

#include "cuttlefish/common/libs/transport/channel_sharedfd.h"
#include "cuttlefish/common/libs/utils/device_type.h"
#include "cuttlefish/host/commands/sensors_simulator/sensors_hal_proxy.h"
#include "cuttlefish/host/commands/sensors_simulator/sensors_simulator.h"
#include "cuttlefish/host/libs/config/logging.h"

DEFINE_int32(control_from_guest_fd, -1, "Sensors control virtio-console from guest to host");
DEFINE_int32(control_to_guest_fd, -1, "Sensors control virtio-console from host to guest");
DEFINE_int32(data_from_guest_fd, -1, "Sensors data virtio-console from guest to host");
DEFINE_int32(data_to_guest_fd, -1, "Sensors data virtio-console from host to guest");
DEFINE_int32(webrtc_fd, -1, "A file descriptor to communicate with webrtc");
DEFINE_int32(kernel_events_fd, -1,
             "A pipe for monitoring events based on messages "
             "written to the kernel log. This is used by "
             "SensorsHalProxy to monitor for device reboots.");
DEFINE_int32(device_type, 0, "The form factor of the Cuttlefish instance.");

namespace cuttlefish {
namespace sensors {

namespace {

static constexpr char kReqMisFormatted[] = "The request is mis-formatted.";
static constexpr char kFdNotOpen[] = "Unable to connect: ";

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
    case kUpdateLowLatencyOffBodyDetect: {
      double value;
      CF_EXPECT(static_cast<bool>(ss >> value), kReqMisFormatted);
      sensors_simulator.UpdateLowLatencyOffBodyDetect(value);
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
  SharedFD webrtc_fd = SharedFD::Dup(FLAGS_webrtc_fd);
  close(FLAGS_webrtc_fd);
  if (!webrtc_fd->IsOpen()) {
    LOG(FATAL) << kFdNotOpen << webrtc_fd->StrError();
  }
  SharedFD control_from_guest_fd = SharedFD::Dup(FLAGS_control_from_guest_fd);
  close(FLAGS_control_from_guest_fd);
  if (!control_from_guest_fd->IsOpen()) {
    LOG(FATAL) << kFdNotOpen << control_from_guest_fd->StrError();
  }
  SharedFD control_to_guest_fd = SharedFD::Dup(FLAGS_control_to_guest_fd);
  close(FLAGS_control_to_guest_fd);
  if (!control_to_guest_fd->IsOpen()) {
    LOG(FATAL) << kFdNotOpen << control_to_guest_fd->StrError();
  }
  SharedFD data_from_guest_fd = SharedFD::Dup(FLAGS_data_from_guest_fd);
  close(FLAGS_data_from_guest_fd);
  if (!data_from_guest_fd->IsOpen()) {
    LOG(FATAL) << kFdNotOpen << data_from_guest_fd->StrError();
  }
  SharedFD data_to_guest_fd = SharedFD::Dup(FLAGS_data_to_guest_fd);
  close(FLAGS_data_to_guest_fd);
  if (!data_to_guest_fd->IsOpen()) {
    LOG(FATAL) << kFdNotOpen << data_to_guest_fd->StrError();
  }
  SharedFD kernel_events_fd = SharedFD::Dup(FLAGS_kernel_events_fd);
  close(FLAGS_kernel_events_fd);

  transport::SharedFdChannel channel(webrtc_fd, webrtc_fd);

  auto device_type = static_cast<DeviceType>(FLAGS_device_type);
  SensorsSimulator sensors_simulator(device_type == DeviceType::Auto);
  SensorsHalProxy sensors_hal_proxy(
      control_from_guest_fd, control_to_guest_fd, data_from_guest_fd,
      data_to_guest_fd, kernel_events_fd, sensors_simulator, device_type);
  while (true) {
    auto result = ProcessWebrtcRequest(channel, sensors_simulator);
    if (!result.ok()) {
      LOG(ERROR) << result.error();
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