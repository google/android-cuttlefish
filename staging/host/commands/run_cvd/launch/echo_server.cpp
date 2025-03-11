//
// Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/run_cvd/launch/launch.h"

#include <unordered_set>
#include <vector>

#include "host/commands/run_cvd/launch/grpc_socket_creator.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

class EchoServer : public CommandSource {
 public:
  INJECT(EchoServer(GrpcSocketCreator& grpc_socket))
      : grpc_socket_(grpc_socket) {}

  // CommandSource
  Result<std::vector<Command>> Commands() override {
    if (!Enabled()) {
      return {};
    }
    return single_element_emplace(
        Command(EchoServerBinary())
            .AddParameter("--grpc_uds_path=",
                          grpc_socket_.CreateGrpcSocket(Name())));
  }

  // SetupFeature
  std::string Name() const override { return "EchoServer"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override { return true; }

  GrpcSocketCreator& grpc_socket_;
};

}  // namespace

fruit::Component<fruit::Required<GrpcSocketCreator>> EchoServerComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, EchoServer>()
      .addMultibinding<SetupFeature, EchoServer>();
}

}  // namespace cuttlefish
