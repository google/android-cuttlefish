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
using gnss_grpc_proxy::GpsCoordinates;
using gnss_grpc_proxy::SendGpsCoordinatesReply;
using gnss_grpc_proxy::SendGpsCoordinatesRequest;
using grpc::ClientContext;

namespace cuttlefish {

GnssClient::GnssClient(const std::shared_ptr<grpc::Channel>& channel)
    : stub_(GnssGrpcProxy::NewStub(channel)) {}

Result<grpc::Status> GnssClient::SendGpsLocations(
    int delay, const GpsFixArray& coordinates) {
  // Data we are sending to the server.
  SendGpsCoordinatesRequest request;
  request.set_delay(delay);
  for (const auto& loc : coordinates) {
    GpsCoordinates* curr = request.add_coordinates();
    curr->set_longitude(loc.longitude);
    curr->set_latitude(loc.latitude);
    curr->set_elevation(loc.elevation);
  }

  // Container for the data we expect from the server.
  SendGpsCoordinatesReply reply;
  // Context for the client. It could be used to convey extra information to
  // the server and/or tweak certain RPC behaviors.
  ClientContext context;
  // The actual RPC.
  grpc::Status status = stub_->SendGpsVector(&context, request, &reply);
  // Act upon its status.
  CF_EXPECT(status.ok(), "GPS data sending failed" << status.error_code()
                                                   << ": "
                                                   << status.error_message());

  LOG(DEBUG) << reply.status();

  return status;
}

}  // namespace cuttlefish
