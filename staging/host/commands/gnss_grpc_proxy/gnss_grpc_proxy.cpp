/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include <grpc/grpc.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/server_posix.h>

#include "gnss_grpc_proxy.grpc.pb.h"

#include <signal.h>

#include <chrono>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include <common/libs/fs/shared_buf.h>
#include <common/libs/fs/shared_fd.h>
#include <common/libs/fs/shared_select.h>
#include <host/libs/config/cuttlefish_config.h>
#include <host/libs/config/logging.h>
#include <queue>

using gnss_grpc_proxy::GnssGrpcProxy;
using gnss_grpc_proxy::SendGpsReply;
using gnss_grpc_proxy::SendGpsRequest;
using gnss_grpc_proxy::SendGpsCoordinatesReply;
using gnss_grpc_proxy::SendGpsCoordinatesRequest;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

DEFINE_int32(gnss_in_fd,
             -1,
             "File descriptor for the gnss's input channel");
DEFINE_int32(gnss_out_fd,
             -1,
             "File descriptor for the gnss's output channel");

DEFINE_int32(fixed_location_in_fd, -1,
             "File descriptor for the fixed location input channel");
DEFINE_int32(fixed_location_out_fd, -1,
             "File descriptor for the fixed location output channel");

DEFINE_int32(gnss_grpc_port,
             -1,
             "Service port for gnss grpc");
DEFINE_string(gnss_grpc_socket, "", "Service socket path for gnss grpc");

DEFINE_string(gnss_file_path, "",
              "gnss raw measurement file path for gnss grpc");
DEFINE_string(fixed_location_file_path, "",
              "fixed location file path for gnss grpc");

constexpr char CMD_GET_LOCATION[] = "CMD_GET_LOCATION";
constexpr char CMD_GET_RAWMEASUREMENT[] = "CMD_GET_RAWMEASUREMENT";
constexpr char END_OF_MSG_MARK[] = "\n\n\n\n";

constexpr uint32_t GNSS_SERIAL_BUFFER_SIZE = 4096;

std::string GenerateGpsLine(const std::string& dataPoint) {
  std::string unix_time_millis =
      std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count());
  std::string formatted_location =
      std::string("Fix,GPS,") + dataPoint + "," +
      std::string("0.000000,3.790092,0.000000,") + unix_time_millis + "," +
      std::string("0.086023256,0.0,11529389988248");

  return formatted_location;
}
// Logic and data behind the server's behavior.
class GnssGrpcProxyServiceImpl final : public GnssGrpcProxy::Service {
  public:
   GnssGrpcProxyServiceImpl(cuttlefish::SharedFD gnss_in,
                            cuttlefish::SharedFD gnss_out,
                            cuttlefish::SharedFD fixed_location_in,
                            cuttlefish::SharedFD fixed_location_out)
       : gnss_in_(gnss_in),
         gnss_out_(gnss_out),
         fixed_location_in_(fixed_location_in),
         fixed_location_out_(fixed_location_out) {
          //Set the default GPS delay to 1 second
          fixed_locations_delay_=1000;
         }


   Status SendGps(ServerContext* context, const SendGpsRequest* request,
                  SendGpsReply* reply) override {
     reply->set_reply("Received gps record");
     std::lock_guard<std::mutex> lock(cached_fixed_location_mutex);
     cached_fixed_location = request->gps();
     return Status::OK;
   }


  std::string ConvertCoordinate(gnss_grpc_proxy::GpsCoordinates coordinate){
    std::string latitude = std::to_string(coordinate.latitude());
    std::string longitude = std::to_string(coordinate.longitude());
    std::string elevation = std::to_string(coordinate.elevation());
    std::string result = latitude + "," + longitude + "," + elevation;
    return result;
  }

   Status SendGpsVector(ServerContext* context,
                        const SendGpsCoordinatesRequest* request,
                        SendGpsCoordinatesReply* reply) override {
     reply->set_status(SendGpsCoordinatesReply::OK);//update protobuf reply
     {
       std::lock_guard<std::mutex> lock(fixed_locations_queue_mutex_);
       // Reset local buffers
       fixed_locations_queue_ = {};
       // Make a local copy of the input buffers
       for (auto loc : request->coordinates()) {
         fixed_locations_queue_.push(ConvertCoordinate(loc));
       }
       fixed_locations_delay_ = request->delay();
     }

     return Status::OK;
   }

    void sendToSerial() {
      std::lock_guard<std::mutex> lock(cached_fixed_location_mutex);
      ssize_t bytes_written = cuttlefish::WriteAll(
          fixed_location_in_, cached_fixed_location + END_OF_MSG_MARK);
      if (bytes_written < 0) {
          LOG(ERROR) << "Error writing to fd: " << gnss_in_->StrError();
      }
    }

    void sendGnssRawToSerial() {
      std::lock_guard<std::mutex> lock(cached_gnss_raw_mutex);
      if (!isGnssRawMeasurement(cached_gnss_raw)) {
        return;
      }
      if (previous_cached_gnss_raw == cached_gnss_raw) {
        // Skip for same record
        return;
      } else {
        // Update cached data
        LOG(DEBUG) << "Skip same record";
        previous_cached_gnss_raw = cached_gnss_raw;
      }
      ssize_t bytes_written =
          cuttlefish::WriteAll(gnss_in_, cached_gnss_raw + END_OF_MSG_MARK);
      LOG(DEBUG) << "Send Gnss Raw to serial: bytes_written: " << bytes_written;
      if (bytes_written < 0) {
        LOG(ERROR) << "Error writing to fd: " << gnss_in_->StrError();
      }
    }

    void StartServer() {
      // Create a new thread to handle writes to the gnss and to the any client
      // connected to the socket.
      fixed_location_write_thread_ =
          std::thread([this]() { WriteFixedLocationFromQueue(); });
      measurement_read_thread_ =
          std::thread([this]() { ReadMeasurementLoop(); });
      fixed_location_read_thread_ =
          std::thread([this]() { ReadFixedLocLoop(); });
    }

    void StartReadFixedLocationFileThread() {
      // Create a new thread to read fixed_location data.
      fixed_location_file_read_thread_ =
          std::thread([this]() { ReadFixedLocationFromLocalFile(); });
    }

    void StartReadGnssRawMeasurementFileThread() {
      // Create a new thread to read raw measurement data.
      measurement_file_read_thread_ =
          std::thread([this]() { ReadGnssRawMeasurement(); });
    }

    void ReadFixedLocationFromLocalFile() {
      std::ifstream file(FLAGS_fixed_location_file_path);
      if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
          /* Only support fix location format to make it simple.
           * Records will only contains 'Fix' prefix.
           * Sample line:
           * Fix,GPS,37.7925002,-122.3979132,13.462797,0.000000,48.000000,0.000000,1593029872254,0.581968,0.000000
           * Sending at 1Hz, currently user should provide a fixed location
           * file that has one location per second. need some extra work to
           * make it more generic, i.e. align with the timestamp in the file.
           */
          {
            std::lock_guard<std::mutex> lock(cached_fixed_location_mutex);
            cached_fixed_location = line;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
          file.close();
      } else {
        LOG(ERROR) << "Can not open fixed location file: "
                   << FLAGS_gnss_file_path;
        return;
      }
    }

    void ReadGnssRawMeasurement() {
      std::ifstream file(FLAGS_gnss_file_path);

      if (file.is_open()) {
        std::string line;
        std::string cached_line = "";
        std::string header = "";

        while (!cached_line.empty() || std::getline(file, line)) {
          if (!cached_line.empty()) {
            line = cached_line;
            cached_line = "";
          }

          // Get data header.
          if (header.empty() && android::base::StartsWith(line, "# Raw")) {
            header = line;
            LOG(DEBUG) << "Header: " << header;
            continue;
          }

          // Ignore not raw measurement data.
          if (!android::base::StartsWith(line, "Raw")) {
            continue;
          }

          {
            std::lock_guard<std::mutex> lock(cached_gnss_raw_mutex);
            cached_gnss_raw = header + "\n" + line;

            std::string new_line = "";
            while (std::getline(file, new_line)) {
              // Group raw data by TimeNanos.
              if (getTimeNanosFromLine(new_line) ==
                  getTimeNanosFromLine(line)) {
                cached_gnss_raw += "\n" + new_line;
              } else {
                cached_line = new_line;
                break;
              }
            }
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        file.close();
      } else {
        LOG(ERROR) << "Can not open GNSS Raw file: " << FLAGS_gnss_file_path;
        return;
      }
    }

    ~GnssGrpcProxyServiceImpl() {
      if (fixed_location_file_read_thread_.joinable()) {
        fixed_location_file_read_thread_.join();
      }
      if (fixed_location_write_thread_.joinable()) {
        fixed_location_write_thread_.join();
      }
      if (measurement_file_read_thread_.joinable()) {
        measurement_file_read_thread_.join();
      }
      if (measurement_read_thread_.joinable()) {
        measurement_read_thread_.join();
      }
      if (fixed_location_read_thread_.joinable()) {
        fixed_location_read_thread_.join();
      }
    }

  private:
   void SendCommand(std::string command, cuttlefish::SharedFD source_out,
                    int out_fd) {
     std::vector<char> buffer(GNSS_SERIAL_BUFFER_SIZE);
     std::string cmd_str;
     auto bytes_read = source_out->Read(buffer.data(), buffer.size());
     if (bytes_read > 0) {
       std::string s(buffer.data(), bytes_read);
       cmd_str += s;
       // In case random string sent though /dev/gnss1, cmd_str will
       // auto resize, to get rid of first page.
       if (cmd_str.size() > GNSS_SERIAL_BUFFER_SIZE * 2) {
         cmd_str = cmd_str.substr(cmd_str.size() - GNSS_SERIAL_BUFFER_SIZE);
       }
       if (cmd_str.find(command) != std::string::npos) {
         if (command == CMD_GET_RAWMEASUREMENT) {
           sendGnssRawToSerial();
         } else if (command == CMD_GET_LOCATION) {
           sendToSerial();
         }
         cmd_str = "";
       }
     } else {
       if (source_out->GetErrno() == EAGAIN ||
           source_out->GetErrno() == EWOULDBLOCK) {
         std::this_thread::sleep_for(std::chrono::milliseconds(100));
       } else {
         LOG(ERROR) << "Error reading fd " << out_fd << ": "
                    << " Error code: " << source_out->GetErrno()
                    << " Error sg:" << source_out->StrError();
       }
     }
   }

   [[noreturn]] void ReadMeasurementLoop() {
     int flags = gnss_out_->Fcntl(F_GETFL, 0);
     gnss_out_->Fcntl(F_SETFL, flags | O_NONBLOCK);

     while (true) {
       SendCommand(CMD_GET_RAWMEASUREMENT, gnss_out_, FLAGS_gnss_out_fd);
     }
   }

   [[noreturn]] void ReadFixedLocLoop() {
     int flags2 = fixed_location_out_->Fcntl(F_GETFL, 0);
     fixed_location_out_->Fcntl(F_SETFL, flags2 | O_NONBLOCK);
     while (true) {
       SendCommand(CMD_GET_LOCATION, fixed_location_out_,
                   FLAGS_fixed_location_out_fd);
     }
   }

   [[noreturn]] void WriteFixedLocationFromQueue() {
      while (true) {
         if (!fixed_locations_queue_.empty()) {
         std::string dataPoint = fixed_locations_queue_.front();
         std::string line = GenerateGpsLine(dataPoint);
         std::lock_guard<std::mutex> lock(cached_fixed_location_mutex);
         cached_fixed_location = line;
         {
           std::lock_guard<std::mutex> lock(fixed_locations_queue_mutex_);
           fixed_locations_queue_.pop();
         }
       }
       std::this_thread::sleep_for(std::chrono::milliseconds(fixed_locations_delay_));
     }
   }

    std::string getTimeNanosFromLine(const std::string& line) {
      // TimeNanos is in column #3.
      std::vector<std::string> vals = android::base::Split(line, ",");
      return vals.size() >= 3 ? vals[2] : "-1";
    }

    bool isGnssRawMeasurement(const std::string& inputStr) {
      // TODO: add more logic check to by pass invalid data.
      return !inputStr.empty() && android::base::StartsWith(inputStr, "# Raw");
    }

    cuttlefish::SharedFD gnss_in_;
    cuttlefish::SharedFD gnss_out_;
    cuttlefish::SharedFD fixed_location_in_;
    cuttlefish::SharedFD fixed_location_out_;

    std::thread measurement_read_thread_;
    std::thread fixed_location_read_thread_;
    std::thread fixed_location_file_read_thread_;
    std::thread fixed_location_write_thread_;
    std::thread measurement_file_read_thread_;

    std::string cached_fixed_location;
    std::mutex cached_fixed_location_mutex;

    std::string cached_gnss_raw;
    std::string previous_cached_gnss_raw;
    std::mutex cached_gnss_raw_mutex;

    std::queue<std::string> fixed_locations_queue_;
    std::mutex fixed_locations_queue_mutex_;
    int fixed_locations_delay_;
};

void RunServer() {
  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  auto gnss_in = cuttlefish::SharedFD::Dup(FLAGS_gnss_in_fd);
  close(FLAGS_gnss_in_fd);
  if (!gnss_in->IsOpen()) {
    LOG(ERROR) << "Error dupping fd " << FLAGS_gnss_in_fd << ": "
               << gnss_in->StrError();
    return;
  }
  close(FLAGS_gnss_in_fd);

  auto gnss_out = cuttlefish::SharedFD::Dup(FLAGS_gnss_out_fd);
  close(FLAGS_gnss_out_fd);
  if (!gnss_out->IsOpen()) {
    LOG(ERROR) << "Error dupping fd " << FLAGS_gnss_out_fd << ": "
               << gnss_out->StrError();
    return;
  }

  auto fixed_location_in =
      cuttlefish::SharedFD::Dup(FLAGS_fixed_location_in_fd);
  close(FLAGS_fixed_location_in_fd);
  if (!fixed_location_in->IsOpen()) {
    LOG(ERROR) << "Error dupping fd " << FLAGS_fixed_location_in_fd << ": "
               << fixed_location_in->StrError();
    return;
  }
  close(FLAGS_fixed_location_in_fd);

  auto fixed_location_out =
      cuttlefish::SharedFD::Dup(FLAGS_fixed_location_out_fd);
  close(FLAGS_fixed_location_out_fd);
  if (!fixed_location_out->IsOpen()) {
    LOG(ERROR) << "Error dupping fd " << FLAGS_fixed_location_out_fd << ": "
               << fixed_location_out->StrError();
    return;
  }

  auto server_address("0.0.0.0:" + std::to_string(FLAGS_gnss_grpc_port));
  GnssGrpcProxyServiceImpl service(gnss_in, gnss_out, fixed_location_in,
                                   fixed_location_out);
  service.StartServer();
  if (!FLAGS_gnss_file_path.empty()) {
    // TODO: On-demand start the read file threads according to data type.
    service.StartReadFixedLocationFileThread();
    service.StartReadGnssRawMeasurementFileThread();

    // In the local mode, we are not start a grpc server, use a infinite loop instead
    while(true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
  } else {
    ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    if (!FLAGS_gnss_grpc_socket.empty()) {
      builder.AddListeningPort("unix:" + FLAGS_gnss_grpc_socket,
                               grpc::InsecureServerCredentials());
    }
    // Register "service" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *synchronous* service.
    builder.RegisterService(&service);
    // Finally assemble the server.
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    // Wait for the server to shutdown. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    server->Wait();
  }

}


int main(int argc, char** argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);

  LOG(DEBUG) << "Starting gnss grpc proxy server...";
  RunServer();

  return 0;
}
