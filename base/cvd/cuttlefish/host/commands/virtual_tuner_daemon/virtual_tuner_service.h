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

#include <grpcpp/grpcpp.h>

#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.grpc.pb.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"

namespace cuttlefish {
namespace virtualtuner {

class VirtualTunerServiceImpl final : public VirtualTuner::Service {
 public:
  explicit VirtualTunerServiceImpl(TunerState& tuner_state);

  ::grpc::Status Tune(::grpc::ServerContext* context,
                      const TuneRequest* request,
                      TuneResponse* response) override;

  ::grpc::Status Stop(::grpc::ServerContext* context,
                      const StopRequest* request,
                      StopResponse* response) override;

 private:
  TunerState& tuner_state_;
};

}  // namespace virtualtuner
}  // namespace cuttlefish
