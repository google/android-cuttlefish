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
#include <memory>
#include <string>

#include <grpcpp.h>

#include "gnss_grpc_proxy.grpc.pb.h"

#include <signal.h>

#include <deque>
#include <thread>
#include <vector>

#include <gflags/gflags.h>
#include <android-base/logging.h>

#include <common/libs/fs/shared_fd.h>
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
      ssize_t bytes_written = 0;
      ssize_t bytes_to_write = buffer.size();
      while (bytes_to_write > 0) {
        bytes_written =
            gnss_in_->Write(buffer.c_str() + bytes_written, bytes_to_write);
        if (bytes_written < 0) {
          LOG(ERROR) << "Error writing to fd: " << gnss_in_->StrError();
          // Don't try to write from this buffer anymore, error handling will
          // be done on the reading thread (failed client will be
          // disconnected, on serial gnss failure this process will abort).
          break;
        }
        bytes_to_write -= bytes_written;
      }
      return Status::OK;
    }

  void StartServer() {
    // Create a new thread to handle writes to the gnss and to the any client
    // connected to the socket.
    read_thread_ = std::thread([this]() { ReadLoop(); });
  }
  private:
    [[noreturn]] void ReadLoop() {
      cuttlefish::SharedFDSet read_set;
      read_set.Set(gnss_out_);
      while (true) {
        cuttlefish::Select(&read_set, nullptr, nullptr, nullptr);
        std::vector<char> buffer(4096);
        auto bytes_read = gnss_out_->Read(buffer.data(), buffer.size());
        if (bytes_read <= 0) {
          LOG(ERROR) << "Error reading from gnss output: "
                     << gnss_out_->StrError();
          // This is likely unrecoverable, so exit here
          std::exit(-4);
        } else {
          std::string s(buffer.data(), bytes_read);
        }
      }
    }
    cuttlefish::SharedFD gnss_in_;
    cuttlefish::SharedFD gnss_out_;
    std::thread read_thread_;
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


int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);

  LOG(DEBUG) << "Starting gnss grpc proxy server...";
  RunServer();

  return 0;
}