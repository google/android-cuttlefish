/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <android-base/logging.h>
#include <gflags/gflags.h>
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/location/GnssClient.h"

DEFINE_int32(instance_num, 1, "Which instance to read the configs from");
DEFINE_double(latitude, 37.8000064, "location latitude");
DEFINE_double(longitude, -122.3989209, "location longitude");
DEFINE_double(elevation, 2.5, "location elevation/altitude");

namespace cuttlefish {
namespace {

int UpdateLocationCvdMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto config = CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Failed to obtain config object";
    return 1;
  }

  auto instance = config->ForInstance(FLAGS_instance_num);
  auto server_port = instance.gnss_grpc_proxy_server_port();
  std::string socket_name =
      std::string("localhost:") + std::to_string(server_port);
  LOG(INFO) << "Server port: " << server_port << " socket: " << socket_name
            << std::endl;

  GnssClient gpsclient(
      grpc::CreateChannel(socket_name, grpc::InsecureChannelCredentials()));

  GpsFixArray coordinates;
  GpsFix location;
  location.longitude=FLAGS_longitude;
  location.latitude=FLAGS_latitude;
  location.elevation=FLAGS_elevation;
  coordinates.push_back(location);
  auto status = gpsclient.SendGpsLocations(1000,coordinates);
  CHECK(status.ok()) << "Failed to send gps location data \n";
  if (!status.ok()) {
    return 1;
  }
  return 0;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::UpdateLocationCvdMain(argc, argv);
}
