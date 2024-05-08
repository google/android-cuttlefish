//
// Copyright (C) 2020 The Android Open Source Project
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

#include <signal.h>
#include <unistd.h>

#include <android-base/strings.h>
#include <fmt/format.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/modem_simulator/modem_simulator.h"
#include "host/libs/config/cuttlefish_config.h"

// we can start multiple modems simultaneously; each modem
// will listent to one server fd for incoming sms/phone call
// there should be at least 1 valid fd
DEFINE_string(server_fds, "", "A comma separated list of file descriptors");
DEFINE_int32(sim_type, 1, "Sim type: 1 for normal, 2 for CtsCarrierApiTestCases");

namespace cuttlefish {
namespace {

std::vector<SharedFD> ServerFdsFromCmdline() {
  // Validate the parameter
  std::string fd_list = FLAGS_server_fds;
  for (auto c: fd_list) {
    if (c != ',' && (c < '0' || c > '9')) {
      LOG(ERROR) << "Invalid file descriptor list: " << fd_list;
      std::exit(1);
    }
  }

  auto fds = android::base::Split(fd_list, ",");
  std::vector<SharedFD> shared_fds;
  for (auto& fd_str: fds) {
    auto fd = std::stoi(fd_str);
    auto shared_fd = SharedFD::Dup(fd);
    close(fd);
    shared_fds.push_back(shared_fd);
  }

  return shared_fds;
}

int ModemSimulatorMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, false);

  // Modem simulator log saved in cuttlefish_runtime
  auto config = CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();

  auto modem_log_path = instance.PerInstanceLogPath("modem_simulator.log");

  {
    auto log_path = instance.launcher_log_path();
    std::vector<std::string> log_files{log_path, modem_log_path};
    android::base::SetLogger(LogToStderrAndFiles(log_files));
  }

  LOG(INFO) << "Start modem simulator, server_fds: " << FLAGS_server_fds
            << ", Sim type: " << ((FLAGS_sim_type == 2) ?
                "special for CtsCarrierApiTestCases" : "normal" );

  auto server_fds = ServerFdsFromCmdline();
  if (server_fds.empty()) {
    LOG(ERROR) << "Need to provide server fd";
    return -1;
  }

  NvramConfig::InitNvramConfigService(server_fds.size(), FLAGS_sim_type);

  // Don't get a SIGPIPE from the clients
  if (sigaction(SIGPIPE, nullptr, nullptr) != 0) {
    LOG(ERROR) << "Failed to set SIGPIPE to be ignored: " << strerror(errno);
  }

  auto nvram_config = NvramConfig::Get();
  const auto nvram_config_file = nvram_config->ConfigFileLocation();

  // Start channel monitor, wait for RIL to connect
  int32_t modem_id = 0;
  std::vector<std::unique_ptr<ModemSimulator>> modem_simulators;

  for (auto& fd : server_fds) {
    CHECK(fd->IsOpen()) << "Error creating or inheriting modem simulator server: "
        << fd->StrError();

    auto modem_simulator = std::make_unique<ModemSimulator>(modem_id);
    auto channel_monitor =
        std::make_unique<ChannelMonitor>(*modem_simulator.get(), fd);

    modem_simulator->Initialize(std::move(channel_monitor));

    modem_simulators.emplace_back(std::move(modem_simulator));

    modem_id++;
  }

  // Monitor exit request and
  // remote call, remote sms from other cuttlefish instance
  std::string monitor_socket_name =
      fmt::format("modem_simulator{}", instance.modem_simulator_host_id());

  auto monitor_socket = SharedFD::SocketLocalServer(monitor_socket_name.c_str(),
                                                    true, SOCK_STREAM, 0666);
  if (!monitor_socket->IsOpen()) {
    LOG(ERROR) << "Unable to create monitor socket for modem simulator";
    std::exit(kServerError);
  }

  // Server loop
  while (true) {
    SharedFDSet read_set;
    read_set.Set(monitor_socket);
    int num_fds = Select(&read_set, nullptr, nullptr, nullptr);
    if (num_fds <= 0) {  // Ignore select error
      LOG(ERROR) << "Select call returned error : " << strerror(errno);
    } else if (read_set.IsSet(monitor_socket)) {
      auto conn = SharedFD::Accept(*monitor_socket);
      std::string buf(4, ' ');
      auto read = ReadExact(conn, &buf);
      if (read <= 0) {
        conn->Close();
        LOG(WARNING) << "Detected close from the other side";
        continue;
      }
      if (buf == "STOP") {  // Exit request from parent process
        LOG(INFO) << "Exit request from parent process";
        nvram_config->SaveToFile(nvram_config_file);
        for (auto& modem : modem_simulators) {
          modem->SaveModemState();
        }
        WriteAll(conn, "OK");  // Ignore the return value. Exit anyway.
        std::exit(kSuccess);
      } else if (buf.compare(0, 3, "REM") == 0) {  // REMO for modem id 0 ...
        // Remote request from other cuttlefish instance
        int id = std::stoi(buf.substr(3, 1));
        if (id >= modem_simulators.size()) {
          LOG(ERROR) << "Not supported modem simulator count: " << id;
        } else {
          modem_simulators[id]->SetRemoteClient(conn, true);
        }
      }
    }
  }
  // Until kill or exit
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::ModemSimulatorMain(argc, argv);
}
