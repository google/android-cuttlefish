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

#include "cuttlefish/host/commands/virtual_tuner_daemon/virtual_tuner_service.h"

#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>

#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"

namespace cuttlefish {
namespace virtualtuner {

VirtualTunerServiceImpl::VirtualTunerServiceImpl(TunerState& tuner_state)
    : handler_(tuner_state) {}

::grpc::Status VirtualTunerServiceImpl::Tune(::grpc::ServerContext*,
                                             const TuneRequest* request,
                                             TuneResponse* response) {
  return handler_.Tune(*request, *response);
}

::grpc::Status VirtualTunerServiceImpl::Stop(::grpc::ServerContext*,
                                             const StopRequest*,
                                             StopResponse* response) {
  return handler_.Stop(*response);
}

::grpc::Status VirtualTunerServiceImpl::GetStatus(::grpc::ServerContext*,
                                                  const GetStatusRequest*,
                                                  GetStatusResponse* response) {
  return handler_.GetStatus(*response);
}

::grpc::Status VirtualTunerServiceImpl::GetStations(
    ::grpc::ServerContext*, const GetStationsRequest* request,
    GetStationsResponse* response) {
  return handler_.GetStations(*request, *response);
}

::grpc::Status VirtualTunerServiceImpl::Seek(::grpc::ServerContext*,
                                             const SeekRequest* request,
                                             SeekResponse* response) {
  return handler_.Seek(*request, *response);
}

::grpc::Status VirtualTunerServiceImpl::UpsertStation(
    ::grpc::ServerContext*, const UpsertStationRequest* request,
    UpsertStationResponse* response) {
  return handler_.UpsertStation(*request, *response);
}

::grpc::Status VirtualTunerServiceImpl::RemoveStation(
    ::grpc::ServerContext*, const RemoveStationRequest* request,
    RemoveStationResponse* response) {
  return handler_.RemoveStation(*request, *response);
}

}  // namespace virtualtuner
}  // namespace cuttlefish
