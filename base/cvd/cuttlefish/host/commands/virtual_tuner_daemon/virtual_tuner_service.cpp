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

#include "absl/log/log.h"

#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"

namespace cuttlefish {
namespace virtualtuner {

VirtualTunerServiceImpl::VirtualTunerServiceImpl(TunerState& tuner_state)
    : tuner_state_(tuner_state) {}

::grpc::Status VirtualTunerServiceImpl::Tune(::grpc::ServerContext*,
                                             const TuneRequest* request,
                                             TuneResponse* response) {
  LOG(INFO) << "Tune request received: Band=" << request->band()
            << ", Freq=" << request->frequency_hz() << " Hz"
            << ", HD=" << request->hd_subchannel();

  tuner_state_.SetTune(request->band(), request->frequency_hz(),
                       request->hd_subchannel());

  response->set_success(true);
  response->set_message("Tuned successfully");
  return ::grpc::Status::OK;
}

::grpc::Status VirtualTunerServiceImpl::Stop(::grpc::ServerContext*,
                                             const StopRequest*,
                                             StopResponse* response) {
  LOG(INFO) << "Stop request received. Stopping tuner audio...";
  tuner_state_.Stop();

  response->set_success(true);
  response->set_message("Stopped successfully");
  return ::grpc::Status::OK;
}

}  // namespace virtualtuner
}  // namespace cuttlefish
