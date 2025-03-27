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

#include "host/frontend/webrtc/location_handler.h"

#include <unistd.h>

#include <android-base/logging.h>
#include <fmt/format.h>

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/location/GnssClient.h"

namespace cuttlefish::webrtc_streaming {

LocationHandler::LocationHandler(
    std::function<void(const uint8_t *, size_t)> send_to_client) {}

LocationHandler::~LocationHandler() {}

void LocationHandler::HandleMessage(const float longitude,
                                          const float latitude,
                                          const float elevation) {
  auto config = CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Failed to obtain config object";
    return;
  }
  auto instance = config->ForDefaultInstance();
  std::string socket_name =
      fmt::format("unix:{}.sock",
                  instance.PerInstanceGrpcSocketPath("GnssGrpcProxyServer"));
  GnssClient gpsclient(
      grpc::CreateChannel(socket_name, grpc::InsecureChannelCredentials()));

  GpsFix location;
  location.longitude = longitude;
  location.latitude = latitude;
  location.elevation = elevation;

  GpsFixArray coordinates;
  coordinates.push_back(location);

  Result<void> reply = gpsclient.SendGpsLocations(1000, coordinates);
  if (!reply.ok()) {
    LOG(ERROR) << reply.error().FormatForEnv();
  }
}

}  // namespace cuttlefish::webrtc_streaming
