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

#include "cuttlefish/host/commands/virtual_tuner_daemon/virtual_tuner_control_service.h"

#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>

#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/audio_generator.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {
namespace virtualtuner {

VirtualTunerControlServiceImpl::VirtualTunerControlServiceImpl(
    TunerState& tuner_state)
    : tuner_state_(tuner_state), handler_(tuner_state) {}

::grpc::Status VirtualTunerControlServiceImpl::Tune(::grpc::ServerContext*,
                                                    const TuneRequest* request,
                                                    TuneResponse* response) {
  return handler_.Tune(*request, *response);
}

::grpc::Status VirtualTunerControlServiceImpl::Stop(::grpc::ServerContext*,
                                                    const StopRequest*,
                                                    StopResponse* response) {
  return handler_.Stop(*response);
}

::grpc::Status VirtualTunerControlServiceImpl::GetStatus(
    ::grpc::ServerContext*, const GetStatusRequest*,
    GetStatusResponse* response) {
  return handler_.GetStatus(*response);
}

::grpc::Status VirtualTunerControlServiceImpl::GetStations(
    ::grpc::ServerContext*, const GetStationsRequest* request,
    GetStationsResponse* response) {
  return handler_.GetStations(*request, *response);
}

::grpc::Status VirtualTunerControlServiceImpl::Seek(::grpc::ServerContext*,
                                                    const SeekRequest* request,
                                                    SeekResponse* response) {
  return handler_.Seek(*request, *response);
}

::grpc::Status VirtualTunerControlServiceImpl::UpsertStation(
    ::grpc::ServerContext*, const UpsertStationRequest* request,
    UpsertStationResponse* response) {
  return handler_.UpsertStation(*request, *response);
}

::grpc::Status VirtualTunerControlServiceImpl::RemoveStation(
    ::grpc::ServerContext*, const RemoveStationRequest* request,
    RemoveStationResponse* response) {
  return handler_.RemoveStation(*request, *response);
}

::grpc::Status VirtualTunerControlServiceImpl::SetStationAudio(
    ::grpc::ServerContext*, const SetStationAudioRequest* request,
    SetStationAudioResponse*) {
  if (request->frequency_hz() == 0) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "frequency_hz must be non-zero");
  }
  if (request->hd_subchannel() > kMaxHdSubchannel) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "hd_subchannel must be in 0..7");
  }

  if (!request->wav_path().empty()) {
    Result<void> valid = ValidateWavFile(request->wav_path());
    if (!valid.has_value()) {
      return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                            valid.error().Message());
    }
  }

  tuner_state_.SetStationWavPath(request->band(), request->frequency_hz(),
                                 request->hd_subchannel(), request->wav_path());
  return ::grpc::Status::OK;
}

}  // namespace virtualtuner
}  // namespace cuttlefish
