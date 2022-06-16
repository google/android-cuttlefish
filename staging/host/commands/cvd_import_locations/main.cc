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

#include "host/libs/location/GpsFix.h"
#include "host/libs/location/GpxParser.h"
#include "host/libs/location/KmlParser.h"
#include "host/libs/location/StringParse.h"

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
DEFINE_double(delay, 1.0, "delay interval between different gps_locations");

DEFINE_string(format, "", "gnss raw measurement file path for gnss grpc");
DEFINE_string(file_path, "", "gnss raw measurement file path for gnss grpc");

constexpr char kUsageMessage[] =
    "gps locations import commandline utility\n\n"
    "  Usage: cvd_import_locations [option] command [args...]\n\n"
    "  arguments:\n\n"
    "    --frmat=[format_string]\n"
    "      input file format for cvd_import_locations\n"
    "         \"gpx\" for gpx input data file\n"
    "         \"kml\" for kml input data file\n\n"
    "    --file_path=[path]\n"
    "      gps locations input file path\n"
    "      if path is not specified, error will be reported\n\n"
    "    --delay=[delay_value]\n"
    "      delay between different gps locations ( double , default value is "
    "1.0 second) \n\n"
    "    --instance_num=[integer_value]\n"
    "      running instance number , starts from 1 ( integer , default value "
    "is 1) \n\n"
    "  examples:\n\n"
    "     cvd_import_locations --format=\"gpx\" --file_path=\"input.gpx\"\n"
    "     cvd_import_locations --format=\"kml\" --file_path=\"input.kml\"\n\n"
    "     cvd_import_locations --format=\"gpx\" --file_path=\"input.gpx\" "
    "--delay=.5\n"
    "     cvd_import_locations --format=\"kml\" --file_path=\"input.kml\" "
    "--delay=.5\n\n"
    "     cvd_import_locations --format=\"gpx\" --file_path=\"input.gpx\" "
    "--delay=.5 --instance_num=2\n";

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

int ImportLocationsCvdMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto config = CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Failed to obtain config object";
    return 1;
  }
  std::set<std::string> supportedFormat = {"gpx", "GPX", "kml", "KML"};

  if (supportedFormat.count(FLAGS_format) == 0) {
    LOG(ERROR) << "Unsupported parsing format" << std::endl;
    return 1;
  }
  LOG(INFO) << FLAGS_format << " Supported format" << std::endl;
  auto instance = config->ForInstance(FLAGS_instance_num);
  auto server_port = instance.gnss_grpc_proxy_server_port();
  std::string socket_name =
      std::string("localhost:") + std::to_string(server_port);
  GnssClient gpsclient(
      grpc::CreateChannel(socket_name, grpc::InsecureChannelCredentials()));

  GpsFixArray gps_locations;
  std::string error;
  bool isOk = false;

  LOG(INFO) << "Server port: " << server_port << " socket: " << socket_name
            << std::endl;
  if (FLAGS_format == "gpx" || FLAGS_format == "GPX") {
    isOk =
        GpxParser::parseFile(FLAGS_file_path.c_str(), &gps_locations, &error);
  } else if (FLAGS_format == "kml" || FLAGS_format == "KML") {
    isOk =
        KmlParser::parseFile(FLAGS_file_path.c_str(), &gps_locations, &error);
  }

  LOG(INFO) << "Number of parsed points: " << gps_locations.size() << std::endl;

  if (!isOk) {
    LOG(ERROR) << " Parsing Error: " << error << std::endl;
    return 1;
  }

  for (auto itr : gps_locations) {
    std::string timestamp = " ";
    std::string latitude = std::to_string(itr.latitude);
    std::string longitude = std::to_string(itr.longitude);
    std::string elevation = std::to_string(itr.elevation);

    std::string formatted_location =
        gpsclient.FormatGps(latitude, longitude, elevation, timestamp, true);
    std::string reply = gpsclient.SendGps(formatted_location);
    int delay = (int)(1000 * FLAGS_delay);
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
  }

  LOG(INFO) << "ImportLocationsCvdMain successful";
  return 0;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  gflags::SetUsageMessage(kUsageMessage);
  return cuttlefish::ImportLocationsCvdMain(argc, argv);
}