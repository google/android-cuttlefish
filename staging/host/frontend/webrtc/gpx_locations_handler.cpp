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

#include "host/frontend/webrtc/gpx_locations_handler.h"

#include <unistd.h>

#include <iostream>
#include <string>

#include <android-base/logging.h>

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/location/GnssClient.h"
#include "host/libs/location/GpxParser.h"

namespace cuttlefish::webrtc_streaming {

GpxLocationsHandler::GpxLocationsHandler(
    std::function<void(const uint8_t *, size_t)> send_to_client) {}

GpxLocationsHandler::~GpxLocationsHandler() {}

void GpxLocationsHandler::HandleMessage(const uint8_t *msg, size_t len) {
  LOG(DEBUG) << "ENTER GpxLocationsHandler handleMessage , size: " << len;
  std::string error;
  GpsFixArray coordinates;
  if (!GpxParser::parseString((const char *)&msg[0], len, &coordinates,
                              &error)) {
    LOG(ERROR) << " Parsing Error: " << error << std::endl;
    return;
  }

  LOG(DEBUG) << "Number of parsed points: " << coordinates.size() << std::endl;
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

  Result<void> reply = gpsclient.SendGpsLocations(1000, coordinates);
  if (!reply.ok()) {
    LOG(ERROR) << reply.error().FormatForEnv();
  }
}

}  // namespace cuttlefish::webrtc_streaming
