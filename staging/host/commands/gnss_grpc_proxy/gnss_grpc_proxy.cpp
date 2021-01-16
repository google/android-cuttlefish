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

#include <iostream>
#include <fstream>
#include <memory>
#include <string>

#include <grpcpp.h>

#include "gnss_grpc_proxy.grpc.pb.h"

#include <signal.h>

#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <gflags/gflags.h>
#include <android-base/logging.h>

#include <common/libs/fs/shared_fd.h>
#include <common/libs/fs/shared_buf.h>
#include <common/libs/fs/shared_select.h>
#include <host/libs/config/cuttlefish_config.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using gnss_grpc_proxy::SendNmeaRequest;
using gnss_grpc_proxy::SendNmeaReply;
using gnss_grpc_proxy::GnssGrpcProxy;

DEFINE_int32(gnss_in_fd,
             -1,
             "File descriptor for the gnss's input channel");
DEFINE_int32(gnss_out_fd,
             -1,
             "File descriptor for the gnss's output channel");

DEFINE_int32(gnss_grpc_port,
             -1,
             "Service port for gnss grpc");

DEFINE_string(gnss_file_path,
              "",
              "NMEA file path for gnss grpc");

constexpr char CMD_GET_LOCATION[] = "CMD_GET_LOCATION";
constexpr uint32_t GNSS_SERIAL_BUFFER_SIZE = 4096;
// Logic and data behind the server's behavior.
class GnssGrpcProxyServiceImpl final : public GnssGrpcProxy::Service {
  public:
    GnssGrpcProxyServiceImpl(cuttlefish::SharedFD gnss_in,
                     cuttlefish::SharedFD gnss_out) : gnss_in_(gnss_in),
                                                  gnss_out_(gnss_out) {}
    Status SendNmea(ServerContext* context, const SendNmeaRequest* request,
                    SendNmeaReply* reply) override {
      reply->set_reply("Received nmea record.");

      auto buffer = request->nmea();
      std::lock_guard<std::mutex> lock(cached_nmea_mutex);
      cached_nmea = request->nmea();
      return Status::OK;
    }

    void sendToSerial() {
      LOG(DEBUG) << "Send NMEA to serial:" << cached_nmea;
      std::lock_guard<std::mutex> lock(cached_nmea_mutex);
      ssize_t bytes_written = cuttlefish::WriteAll(gnss_in_, cached_nmea);
      if (bytes_written < 0) {
          LOG(ERROR) << "Error writing to fd: " << gnss_in_->StrError();
      }
    }

    void StartServer() {
      // Create a new thread to handle writes to the gnss and to the any client
      // connected to the socket.
      read_thread_ = std::thread([this]() { ReadLoop(); });
    }

    void StartReadFileThread() {
      // Create a new thread to handle writes to the gnss and to the any client
      // connected to the socket.
      file_read_thread_ = std::thread([this]() { ReadNmeaFromLocalFile(); });
    }

    void ReadNmeaFromLocalFile() {
      std::ifstream file(FLAGS_gnss_file_path);
      if (file.is_open()) {
          std::string line;
          std::string lastLine;
          int count = 0;
          while (std::getline(file, line)) {
              count++;
              /* Only support a lite version of NEMA format to make it simple.
               * Records will only contains $GPGGA, $GPRMC,
               * $GPGGA,213204.00,3725.371240,N,12205.589239,W,7,,0.38,-26.75,M,0.0,M,0.0,0000*78
               * $GPRMC,213204.00,A,3725.371240,N,12205.589239,W,000.0,000.0,290819,,,A*49
               * $GPGGA,....
               * $GPRMC,....
               * Sending at 1Hz, currently user should
               * provide a NMEA file that has one location per second. need some extra work
               * to make it more generic, i.e. align with the timestamp in the file.
               */
              if (count % 2 == 0) {
                {
                  std::lock_guard<std::mutex> lock(cached_nmea_mutex);
                  cached_nmea = lastLine + '\n' + line;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
              }
              lastLine = line;
          }
          file.close();
      } else {
        LOG(ERROR) << "Can not open NMEA file: " << FLAGS_gnss_file_path ;
        return;
      }
    }
  private:
    [[noreturn]] void ReadLoop() {
      cuttlefish::SharedFDSet read_set;
      read_set.Set(gnss_out_);
      std::vector<char> buffer(GNSS_SERIAL_BUFFER_SIZE);
      int total_read = 0;
      std::string gnss_cmd_str;
      int flags = gnss_out_->Fcntl(F_GETFL, 0);
      gnss_out_->Fcntl(F_SETFL, flags | O_NONBLOCK);
      while (true) {
        auto bytes_read = gnss_out_->Read(buffer.data(), buffer.size());
        if (bytes_read > 0) {
          std::string s(buffer.data(), bytes_read);
          gnss_cmd_str += s;
          // In case random string sent though /dev/gnss0, gnss_cmd_str will auto resize,
          // to get rid of first page.
          if (gnss_cmd_str.size() > GNSS_SERIAL_BUFFER_SIZE * 2) {
            gnss_cmd_str = gnss_cmd_str.substr(gnss_cmd_str.size() - GNSS_SERIAL_BUFFER_SIZE);
          }
          total_read += bytes_read;
          if (gnss_cmd_str.find(CMD_GET_LOCATION) != std::string::npos) {
            sendToSerial();
            gnss_cmd_str = "";
            total_read = 0;
          }
        } else {
          if (gnss_out_->GetErrno() == EAGAIN|| gnss_out_->GetErrno() == EWOULDBLOCK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          } else {
            LOG(ERROR) << "Error reading fd " << FLAGS_gnss_out_fd << ": "
              << " Error code: " << gnss_out_->GetErrno()
              << " Error sg:" << gnss_out_->StrError();
          }
        }
      }
    }

    cuttlefish::SharedFD gnss_in_;
    cuttlefish::SharedFD gnss_out_;
    std::thread read_thread_;
    std::thread file_read_thread_;
    std::string cached_nmea;
    std::mutex cached_nmea_mutex;
};

void RunServer() {
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
  auto server_address("0.0.0.0:" + std::to_string(FLAGS_gnss_grpc_port));
  GnssGrpcProxyServiceImpl service(gnss_in, gnss_out);
  service.StartServer();
  if (!FLAGS_gnss_file_path.empty()) {
    service.StartReadFileThread();
    // In the local mode, we are not start a grpc server, use a infinite loop instead
    while(true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
  } else {
    ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
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
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);

  LOG(DEBUG) << "Starting gnss grpc proxy server...";
  RunServer();

  return 0;
}
