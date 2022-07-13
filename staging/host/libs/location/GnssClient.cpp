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

#include "GnssClient.h"
#include <android-base/logging.h>
#include <host/libs/config/logging.h>
#include <cassert>
#include <string>

using gnss_grpc_proxy::GnssGrpcProxy;
using gnss_grpc_proxy::SendGpsReply;
using gnss_grpc_proxy::SendGpsRequest;
using grpc::ClientContext;

namespace cuttlefish {

GnssClient::GnssClient(std::shared_ptr<grpc::Channel> channel)
    : stub_(GnssGrpcProxy::NewStub(channel)) {}

// Assambles the client's payload, sends it and presents the response back
// from the server.
Result<grpc::Status> GnssClient::SendGps(const std::string& user) {
  // Data we are sending to the server.
  SendGpsRequest request;
  request.set_gps(user);
  // Container for the data we expect from the server.
  SendGpsReply reply;
  // Context for the client. It could be used to convey extra information to
  // the server and/or tweak certain RPC behaviors.
  ClientContext context;
  // The actual RPC.
  grpc::Status status = stub_->SendGps(&context, request, &reply);
  // Act upon its status.
  CF_EXPECT(status.ok(), "GPS data sending failed" << status.error_code()
                                                   << ": "
                                                   << status.error_message());

  LOG(INFO) << reply.reply();

  return status;
}

/*
Fix,GPS,      37.8000064,     -122.3989209,   -42.139252, 0.000000,3.790092,
0.000000,     1622580024000,  0.086023256,    0.0, 11529389988248"
Fix,Provider,
LatitudeDegrees,LongitudeDegrees,AltitudeMeters,SpeedMps,AccuracyMeters,BearingDegrees,UnixTimeMillis,SpeedAccuracyMps,BearingAccuracyDegrees,elapsedRealtimeNanos
*/
std::string GnssClient::FormatGps(const std::string& latitude,
                                  const std::string& longitude,
                                  const std::string& elevation,
                                  const std::string& timestamp,
                                  bool inject_time) {
  std::string unix_time_millis;
  if (inject_time) {
    unix_time_millis =
        std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count());
  } else {
    unix_time_millis = timestamp;
  }
  std::string formatted_location =
      std::string("Fix,GPS,") + latitude + "," + longitude + "," + elevation +
      "," + std::string("0.000000,3.790092,0.000000,") + unix_time_millis +
      "," + std::string("0.086023256,0.0,11529389988248");
  LOG(INFO) << "Location: " << formatted_location << std::endl;

  return formatted_location;
}
}  // namespace cuttlefish