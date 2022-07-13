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

#pragma once
#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include "common/libs/utils/result.h"
#include "gnss_grpc_proxy.grpc.pb.h"

namespace cuttlefish {
class GnssClient {
 public:
  GnssClient(std::shared_ptr<grpc::Channel> channel);

  // Assambles the client's payload, sends it and presents the response back
  // from the server.
  Result<grpc::Status> SendGps(const std::string& user);
  std::string FormatGps(const std::string& latitude,
                        const std::string& longitude,
                        const std::string& elevation,
                        const std::string& timestamp, bool inject_time);

 private:
  std::unique_ptr<gnss_grpc_proxy::GnssGrpcProxy::Stub> stub_;
};
}  // namespace cuttlefish