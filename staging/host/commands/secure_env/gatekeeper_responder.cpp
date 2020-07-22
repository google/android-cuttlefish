//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "gatekeeper_responder.h"

#include <android-base/logging.h>
#include <gatekeeper/gatekeeper_messages.h>

GatekeeperResponder::GatekeeperResponder(
    cuttlefish::GatekeeperChannel* channel, gatekeeper::GateKeeper* gatekeeper)
    : channel_(channel), gatekeeper_(gatekeeper) {
}

bool GatekeeperResponder::ProcessMessage() {
  auto request = channel_->ReceiveMessage();
  if (!request) {
    LOG(ERROR) << "Could not receive message";
    return false;
  }
  const uint8_t* buffer = request->payload;
  const uint8_t* buffer_end = request->payload + request->payload_size;
  switch(request->cmd) {
    using namespace gatekeeper;
    case ENROLL: {
      EnrollRequest enroll_request;
      auto rc = enroll_request.Deserialize(buffer, buffer_end);
      if (rc != ERROR_NONE) {
        LOG(ERROR) << "Failed to deserialize Enroll Request";
        return false;
      }
      EnrollResponse response;
      gatekeeper_->Enroll(enroll_request, &response);
      return channel_->SendResponse(ENROLL, response);
    }
    case VERIFY: {
      VerifyRequest verify_request;
      auto rc = verify_request.Deserialize(buffer, buffer_end);
      if (rc != ERROR_NONE) {
        LOG(ERROR) << "Failed to deserialize Verify Request";
        return false;
      }
      VerifyResponse response;
      gatekeeper_->Verify(verify_request, &response);
      return channel_->SendResponse(VERIFY, response);
    }
    default:
      LOG(ERROR) << "Unrecognized message id " << request->cmd;
      return false;
  }
}
