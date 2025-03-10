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
#include <common/libs/fs/shared_buf.h>
#include <gflags/gflags.h>
#include <host/libs/config/logging.h>
#include "host/libs/config/cuttlefish_config.h"

#include "host/libs/location/GnssClient.h"
#include "host/libs/location/GpxParser.h"
#include "host/libs/location/KmlParser.h"

DEFINE_int32(instance_num, 1, "Which instance to read the configs from");
DEFINE_double(delay, 1.0, "delay interval between different coordinates");

DEFINE_string(format, "", "supported file format, either kml or gpx");
DEFINE_string(file_path, "", "path to input file location {Kml or gpx} format");

const char* kUsageMessage = R""""(gps locations import commandline utility

Usage: cvd_import_locations [option] command [args...]

arguments:

  --format=[format_string]
    input file format for cvd_import_locations
        "gpx" for gpx input data file
        "kml" for kml input data file

  --file_path=[path]
    gps locations input file path
    if path is not specified, error will be reported

  --delay=[delay_value]
    delay between different gps locations ( double , default value is 1.0 second)

  --instance_num=[integer_value]
    running instance number , starts from 1 ( integer , default value is 1)

examples:

    cvd_import_locations --format="gpx" --file_path="input.gpx"
    cvd_import_locations --format="kml" --file_path="input.kml"

    cvd_import_locations --format="gpx" --file_path="input.gpx" --delay=.5
    cvd_import_locations --format="kml" --file_path="input.kml" --delay=.5

    cvd_import_locations --format="gpx" --file_path="input.gpx" --delay=.5 --instance_num=2

)"""";
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

  GpsFixArray coordinates;
  std::string error;
  bool isOk = false;

  LOG(INFO) << "Server port: " << server_port << " socket: " << socket_name
            << std::endl;
  if (FLAGS_format == "gpx" || FLAGS_format == "GPX") {
    isOk =
        GpxParser::parseFile(FLAGS_file_path.c_str(), &coordinates, &error);
  } else if (FLAGS_format == "kml" || FLAGS_format == "KML") {
    isOk =
        KmlParser::parseFile(FLAGS_file_path.c_str(), &coordinates, &error);
  }

  LOG(INFO) << "Number of parsed points: " << coordinates.size() << std::endl;

  if (!isOk) {
    LOG(ERROR) << " Parsing Error: " << error << std::endl;
    return 1;
  }

  int delay = (int)(1000 * FLAGS_delay);
  auto status = gpsclient.SendGpsLocations(delay,coordinates);
  CHECK(status.ok()) << "Failed to send gps location data \n";
  if (!status.ok()) {
    return 1;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(delay));
  return 0;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  gflags::SetUsageMessage(kUsageMessage);
  return cuttlefish::ImportLocationsCvdMain(argc, argv);
}