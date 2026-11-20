/*
 * Copyright (C) 2026 The Android Open Source Project
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

#pragma once

#include <grpcpp/support/status.h>

#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"

namespace cuttlefish {
namespace virtualtuner {

// Common tuner and station control operations shared by the guest-facing
// VirtualTuner service and the host-only VirtualTunerControlService.
class VirtualTunerHandler {
 public:
  explicit VirtualTunerHandler(TunerState& tuner_state);
  VirtualTunerHandler(const VirtualTunerHandler&) = delete;
  VirtualTunerHandler& operator=(const VirtualTunerHandler&) = delete;

  ::grpc::Status Tune(const TuneRequest& request, TuneResponse& response);
  ::grpc::Status Stop(StopResponse& response);
  ::grpc::Status GetStatus(GetStatusResponse& response) const;
  ::grpc::Status GetStations(const GetStationsRequest& request,
                             GetStationsResponse& response) const;
  ::grpc::Status Seek(const SeekRequest& request, SeekResponse& response) const;
  ::grpc::Status UpsertStation(const UpsertStationRequest& request,
                               UpsertStationResponse& response);
  ::grpc::Status RemoveStation(const RemoveStationRequest& request,
                               RemoveStationResponse& response);

 private:
  TunerState& tuner_state_;
};

}  // namespace virtualtuner
}  // namespace cuttlefish
