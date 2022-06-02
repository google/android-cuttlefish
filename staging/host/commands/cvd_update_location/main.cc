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

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include <common/libs/fs/shared_buf.h>
#include <common/libs/fs/shared_fd.h>
#include <common/libs/fs/shared_select.h>
#include <host/libs/config/logging.h>
#include "host/libs/config/cuttlefish_config.h"

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include "common/libs/utils/environment.h"
#include "gnss_grpc_proxy.grpc.pb.h"
#include "host/commands/run_cvd/runner_defs.h"

using gnss_grpc_proxy::GnssGrpcProxy;
using gnss_grpc_proxy::SendGpsReply;
using gnss_grpc_proxy::SendGpsRequest;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

DEFINE_int32(instance_num, 1, "Which instance to read the configs from");
DEFINE_string(latitude, "37.8000064", "location latitude");
DEFINE_string(longitude, "-122.3989209", "location longitude");
DEFINE_string(elevation, "2.5", "location elevation/altitude");

class GnssClient {
 public:
  GnssClient(std::shared_ptr<Channel> channel)
      : stub_(GnssGrpcProxy::NewStub(channel)) {}
  // Assambles the client's payload, sends it and presents the response back
  // from the server.
  std::string SendGps(const std::string& user) {
    // Data we are sending to the server.
    SendGpsRequest request;
    request.set_gps(user);
    // Container for the data we expect from the server.
    SendGpsReply reply;
    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;
    // The actual RPC.
    Status status = stub_->SendGps(&context, request, &reply);
    // Act upon its status.
    if (status.ok()) {
      LOG(INFO) << "GPS data sending successful " << std::endl;
      return reply.reply();
    } else {
      LOG(INFO) << "GPS data sending failed" << status.error_code() << ": "
                << status.error_message() << std::endl;
      return "RPC failed";
    }
  }
  /*
    Fix,GPS,      37.8000064,     -122.3989209,   -42.139252, 0.000000,3.790092,
    0.000000,     1622580024000,  0.086023256,    0.0, 11529389988248"
    Fix,Provider,
    LatitudeDegrees,LongitudeDegrees,AltitudeMeters,SpeedMps,AccuracyMeters,BearingDegrees,UnixTimeMillis,SpeedAccuracyMps,BearingAccuracyDegrees,elapsedRealtimeNanos
  */
  std::string FormatGps(const std::string& latitude,
                        const std::string& longitude,
                        const std::string& elevation,
                        const std::string& timestamp, bool inject_time) {
    std::string unix_time_millis;
    if (inject_time) {
      unix_time_millis = std::to_string(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count());
    } else {
      unix_time_millis = timestamp;
    }
    std::string formatted_location =
        std::string("Fix,GPS,") + latitude + "," + longitude + "," + elevation +
        "," + std::string("0.000000,3.790092,0.000000,") + unix_time_millis +
        "," + std::string("0.086023256,0.0,11529389988248");
    LOG(INFO) << "Location: " << formatted_location << std::endl;

    return formatted_location;
  }

 private:
  std::unique_ptr<GnssGrpcProxy::Stub> stub_;
};

namespace cuttlefish {
namespace {

int UpdateLocationCvdMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto config = CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Failed to obtain config object";
    return 1;
  }

  auto instance = config->ForInstance(FLAGS_instance_num);
  auto server_port = instance.gnss_grpc_proxy_server_port();
  std::string socket_name =
      std::string("localhost:") + std::to_string(server_port);
  GnssClient gpsclient(
      grpc::CreateChannel(socket_name, grpc::InsecureChannelCredentials()));
  std::string timestamp = " ";
  std::string formatted_location = gpsclient.FormatGps(
      FLAGS_latitude, FLAGS_longitude, FLAGS_elevation, timestamp, true);
  std::string reply = gpsclient.SendGps(formatted_location);

  LOG(INFO) << "Server port: " << server_port << " socket: " << socket_name
            << std::endl;
  LOG(INFO) << "GnssGrpcProxy received: " << reply << std::endl;
  LOG(INFO) << "UpdateLocationCvdMain successful";
  return 0;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::UpdateLocationCvdMain(argc, argv);
}
