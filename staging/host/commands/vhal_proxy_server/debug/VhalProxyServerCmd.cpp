/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "VehicleServer.grpc.pb.h"
#include "VehicleServer.pb.h"

#include <android-base/logging.h>
#include <grpc++/grpc++.h>
#include "common/libs/utils/flag_parser.h"

using ::android::hardware::automotive::vehicle::proto::DumpOptions;
using ::android::hardware::automotive::vehicle::proto::DumpResult;
using ::android::hardware::automotive::vehicle::proto::VehicleServer;
using ::cuttlefish::Flag;
using ::cuttlefish::FlagAliasMode;
using ::cuttlefish::GflagsCompatFlag;
using ::grpc::ClientContext;
using ::grpc::CreateChannel;
using ::grpc::InsecureChannelCredentials;
using ::grpc::Status;

static constexpr int DEFAULT_ETH_PORT = 9300;

// A GRPC server for VHAL running on the guest Android.
// argv[1]: Config directory path containing property config file (e.g.
// DefaultProperties.json).
// argv[2]: The vsock port number used by this server.
int main(int argc, char* argv[]) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    args.push_back(std::string(argv[i]));
  }

  int32_t eth_port = DEFAULT_ETH_PORT;
  std::vector<Flag> flags{GflagsCompatFlag("port", eth_port)};
  CHECK(cuttlefish::ConsumeFlags(flags, args).ok()) << "Failed to parse flags";

  DumpOptions dump_options;
  // The rest of the arguments are commands passed to VHAL.
  for (const auto& arg : args) {
    dump_options.add_options(arg);
  }

  auto eth_addr = fmt::format("localhost:{}", eth_port);

  auto channel = CreateChannel(eth_addr, InsecureChannelCredentials());
  auto stub = VehicleServer::NewStub(channel);
  ClientContext context;
  DumpResult result;
  auto status = stub->Dump(&context, dump_options, &result);
  CHECK(status.ok()) << "Failed to call Dump on VHAL proxy server, error: "
                     << status.error_message();

  std::cout << "Debug command finished, result: \n" << result.buffer();
  return 0;
}
