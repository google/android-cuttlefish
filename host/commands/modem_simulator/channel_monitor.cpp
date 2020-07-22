
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

#include "host/commands/modem_simulator/channel_monitor.h"

#include <android-base/strings.h>

#include <algorithm>

#include "host/commands/modem_simulator/modem_simulator.h"

namespace cuttlefish {

constexpr int32_t kMaxCommandLength = 4096;

Client::Client(cuttlefish::SharedFD fd) : client_fd(fd) {}

Client::Client(cuttlefish::SharedFD fd, ClientType client_type)
    : type(client_type), client_fd(fd) {}

bool Client::operator==(const Client& other) const {
  return client_fd == other.client_fd;
}

void Client::SendCommandResponse(std::string response) const {
  if (response.empty()) {
    LOG(DEBUG) << "Invalid response, ignore!";
    return;
  }

  if (response.back() != '\r') {
    response += '\r';
  }
  LOG(DEBUG) << " AT< " << response;

  std::lock_guard<std::mutex> autolock(const_cast<Client*>(this)->write_mutex);
  client_fd->Write(response.data(), response.size());
}

void Client::SendCommandResponse(
    const std::vector<std::string>& responses) const {
  for (auto& response : responses) {
    SendCommandResponse(response);
  }
}

ChannelMonitor::ChannelMonitor(ModemSimulator* modem,
                               cuttlefish::SharedFD server)
    : modem_(modem), server_(server) {
  if (!cuttlefish::SharedFD::Pipe(&read_pipe_, &write_pipe_)) {
    LOG(ERROR) << "Unable to create pipe, ignore";
  }

  if (server_->IsOpen())
    monitor_thread_ = std::thread([this]() { MonitorLoop(); });
}

void ChannelMonitor::SetRemoteClient(cuttlefish::SharedFD client, bool is_accepted) {
  auto remote_client = std::make_unique<Client>(client, Client::REMOTE);

  if (is_accepted) {
    // There may be new data from remote client before select.
    remote_client->first_read_command_ = true;
    ReadCommand(*remote_client);
  }

  remote_client->first_read_command_ = false;
  remote_clients_.push_back(std::move(remote_client));
  LOG(DEBUG) << "added one remote client";

  // Trigger monitor loop
  if (write_pipe_->IsOpen()) {
    write_pipe_->Write("OK", sizeof("OK"));
  } else {
    LOG(ERROR) << "Pipe created fail, can't trigger monitor loop";
  }
}

void ChannelMonitor::AcceptIncomingConnection() {
  auto client_fd  = cuttlefish::SharedFD::Accept(*server_);
  if (!client_fd->IsOpen()) {
    LOG(ERROR) << "Error accepting connection on socket: " << client_fd->StrError();
  } else {
    auto client = std::make_unique<Client>(client_fd);
    LOG(DEBUG) << "added one RIL client";
    clients_.push_back(std::move(client));
    if (clients_.size() == 1) {
      // The first connected client default to be the unsolicited commands channel
      modem_->OnFirstClientConnected();
    }
  }
}

void ChannelMonitor::ReadCommand(Client& client) {
  std::vector<char> buffer(kMaxCommandLength);
  auto bytes_read = client.client_fd->Read(buffer.data(), buffer.size());
  if (bytes_read <= 0) {
    if (errno == EAGAIN && client.type == Client::REMOTE &&
        client.first_read_command_) {
      LOG(ERROR) << "After read 'REM' from remote client, and before select "
          "no new data come.";
      return;
    }
    LOG(ERROR) << "Error reading from client fd: " << client.client_fd->StrError();
    client.client_fd->Close();  // Ignore errors here
    // Erase client from the vector clients
    auto& clients = client.type == Client::REMOTE ? remote_clients_ : clients_;
    auto iter = std::find_if(
        clients.begin(), clients.end(),
        [&](std::unique_ptr<Client>& other) { return *other == client; });
    if (iter != clients.end()) {
      clients.erase(iter);
    }
    return;
  }

  std::string& incomplete_command = client.incomplete_command;

  // Add the incomplete command from the last read
  auto commands = std::string{incomplete_command.data()};
  commands.append(buffer.data());

  incomplete_command.clear();

  // Replacing '\n' with '\r'
  commands = android::base::StringReplace(commands, "\n", "\r", true);

  // Split into commands and dispatch
  size_t pos = 0, r_pos = 0;  // '\r' or '\n'
  while (r_pos != std::string::npos) {
    if (modem_->IsWaitingSmsPdu()) {
      r_pos = commands.find('\032', pos);  // In sms, find ctrl-z
    } else {
      r_pos = commands.find('\r', pos);
    }
    if (r_pos != std::string::npos) {
      auto command = commands.substr(pos, r_pos - pos);
      if (command.size() > 0) {  // "\r\r" ?
        LOG(DEBUG) << "AT> " << command;
        modem_->DispatchCommand(client, command);
      }
      pos = r_pos + 1;  // Skip '\r'
    } else if (pos < commands.length()) {  // Incomplete command
      incomplete_command = commands.substr(pos);
      LOG(DEBUG) << "incomplete command: " << incomplete_command;
    }
  }
}

void ChannelMonitor::SendUnsolicitedCommand(std::string& response) {
  // The first accepted client default to be unsolicited command channel?
  auto iter = clients_.begin();
  if (iter != clients_.end()) {
    iter->get()->SendCommandResponse(response);
  } else {
    LOG(DEBUG) << "No client connected yet.";
  }
}

void ChannelMonitor::SendRemoteCommand(cuttlefish::SharedFD client, std::string& response) {
  auto iter = remote_clients_.begin();
  for (; iter != remote_clients_.end(); ++iter) {
    if (iter->get()->client_fd == client) {
      iter->get()->SendCommandResponse(response);
      return;
    }
  }
  LOG(DEBUG) << "Remote client has closed.";
}

void ChannelMonitor::CloseRemoteConnection(cuttlefish::SharedFD client) {
  auto iter = remote_clients_.begin();
  for (; iter != remote_clients_.end(); ++iter) {
    if (iter->get()->client_fd == client) {
      iter->get()->client_fd->Close();
      iter->get()->is_valid = false;

      // Trigger monitor loop
      if (write_pipe_->IsOpen()) {
        write_pipe_->Write("OK", sizeof("OK"));
        LOG(DEBUG) << "asking to remove clients";
      } else {
        LOG(ERROR) << "Pipe created fail, can't trigger monitor loop";
      }
      return;
    }
  }
  LOG(DEBUG) << "Remote client has been erased.";
}

ChannelMonitor::~ChannelMonitor() {
  if (write_pipe_->IsOpen()) {
    write_pipe_->Write("KO", sizeof("KO"));
  }

  if (monitor_thread_.joinable()) {
    LOG(DEBUG) << "waiting for monitor thread to join";
    monitor_thread_.join();
  }
}

static void removeInvalidClients(std::vector<std::unique_ptr<Client>>& clients) {
  auto iter = clients.begin();
  for (; iter != clients.end();) {
    if (iter->get()->is_valid) {
      ++iter;
    } else {
      LOG(DEBUG) << "removed 1 client";
      iter = clients.erase(iter);
    }
  }
}

void ChannelMonitor::MonitorLoop() {
  do {
    cuttlefish::SharedFDSet read_set;
    read_set.Set(server_);
    read_set.Set(read_pipe_);
    for (auto& client: clients_) {
      if (client->is_valid) read_set.Set(client->client_fd);
    }
    for (auto& client: remote_clients_) {
      if (client->is_valid) read_set.Set(client->client_fd);
    }
    int num_fds = cuttlefish::Select(&read_set, nullptr, nullptr, nullptr);
    if (num_fds < 0) {
      LOG(ERROR) << "Select call returned error : " << strerror(errno);
      // std::exit(kSelectError);
      break;
    } else if (num_fds > 0) {
      if (read_set.IsSet(server_)) {
        AcceptIncomingConnection();
      }
      if (read_set.IsSet(read_pipe_)) {
        std::string buf(2, ' ');
        read_pipe_->Read(buf.data(), buf.size());  // Empty pipe
        if (buf == std::string("KO")) {
          LOG(DEBUG) << "requested to exit now";
          break;
        }
        // clean the lists
        removeInvalidClients(clients_);
        removeInvalidClients(remote_clients_);
      }
      for (auto& client : clients_) {
        if (read_set.IsSet(client->client_fd)) {
          ReadCommand(*client);
        }
      }
      for (auto& client : remote_clients_) {
        if (read_set.IsSet(client->client_fd)) {
          ReadCommand(*client);
        }
      }
    } else {
      // Ignore errors here
      LOG(ERROR) << "Select call returned error : " << strerror(errno);
    }
  } while (true);
}

}  // namespace cuttlefish
