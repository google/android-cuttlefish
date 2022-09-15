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

#include "host/frontend/webrtc/kml_locations_handler.h"
#include <android-base/logging.h>
#include <unistd.h>
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/location/GnssClient.h"
#include "host/libs/location/KmlParser.h"
#include "string.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

namespace cuttlefish {
namespace webrtc_streaming {

KmlLocationsHandler::KmlLocationsHandler(
    std::function<void(const uint8_t *, size_t)> send_to_client) {}

KmlLocationsHandler::~KmlLocationsHandler() {}

void KmlLocationsHandler::HandleMessage(const uint8_t *msg, size_t len) {
  LOG(DEBUG) << "ENTER KmlLocationsHandler handleMessage , size: " << len;
  std::string error;
  GpsFixArray coordinates;
  if (!KmlParser::parseString((const char *)&msg[0], len, &coordinates,
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
  auto server_port = instance.gnss_grpc_proxy_server_port();
  std::string socket_name =
      std::string("localhost:") + std::to_string(server_port);
  LOG(DEBUG) << "Server port: " << server_port << " socket: " << socket_name
             << std::endl;


  GnssClient gpsclient(
      grpc::CreateChannel(socket_name, grpc::InsecureChannelCredentials()));


  auto reply = gpsclient.SendGpsLocations(1000,coordinates);
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
