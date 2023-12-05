/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <android-base/logging.h>
#include <android-base/result.h>
#include <gtest/gtest.h>
#include <sys/socket.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/result_matchers.h"
#include "common/libs/utils/vsock_connection.h"
#include "vsock_connection.h"

namespace cuttlefish {

namespace {

using ::testing::Eq;

}  // namespace

constexpr unsigned int kDefaultPort = 7600;
constexpr unsigned int kDefaultCID = 1;

// Classes that use a normal socket for testing
class FakeVsockClientConnection : public VsockClientConnection {
 protected:
  SharedFD CreateSocket(unsigned int port, unsigned int cid,
                        bool vhost_user) override;
};

class FakeVsockServer : public VsockServer {
 protected:
  SharedFD CreateSocket(unsigned int port, unsigned int cid,
                        std::optional<int> vhost_user_vsock_cid) override;
};

SharedFD FakeVsockClientConnection::CreateSocket(unsigned int port,
                                                 unsigned int cid,
                                                 bool /* vhost_user */) {
  return SharedFD::SocketLocalClient(fmt::format("{}:{}", port, cid), false,
                                     SOCK_STREAM | SOCK_NONBLOCK);
}

SharedFD FakeVsockServer::CreateSocket(
    unsigned int port, unsigned int cid,
    std::optional<int> /* vhost_user_vsock_cid */) {
  auto name = std::to_string(port) + ":" + std::to_string(cid);
  return SharedFD::SocketLocalServer(name, false, SOCK_STREAM | SOCK_NONBLOCK,
                                     0666);
}

class VsockConnectionTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_THAT(server_.Start(kDefaultPort, kDefaultCID, std::nullopt), IsOk());
    client_socket_ = std::make_unique<FakeVsockClientConnection>();
    auto server_socket_future = server_.AcceptConnectionAsync();
    auto client_connected = false;
    while (!client_connected) {
      // fail if the server has timed out or errored before client connects
      ASSERT_NE(server_socket_future.wait_for(std::chrono::seconds(0)),
                std::future_status::ready);

      client_connected =
          client_socket_->Connect(kDefaultPort, kDefaultCID, false);
      if (!client_connected) {
        LOG(INFO) << "Failed to connect. Retrying.";
      }
    }

    auto server_result = server_socket_future.get();
    ASSERT_THAT(server_result, IsOk());
    server_socket_ = std::move(*server_result);
    ASSERT_TRUE(client_connected && server_socket_->IsConnected());
  }

  FakeVsockServer server_;
  std::unique_ptr<VsockClientConnection> client_socket_;
  std::unique_ptr<VsockConnection> server_socket_;
};

TEST_F(VsockConnectionTest, Connect) {
  ASSERT_TRUE(server_.IsRunning());
  ASSERT_TRUE(server_socket_->IsConnected());
  ASSERT_TRUE(client_socket_->IsConnected());
}

TEST_F(VsockConnectionTest, BasicReadWrite) {
  int32_t test_val = 123;
  ASSERT_THAT(client_socket_->Write(test_val), IsOk());
  ASSERT_THAT(server_socket_->ReadInt32(), IsOkAndValue(Eq(test_val)));

  test_val = 323;
  ASSERT_THAT(server_socket_->Write(test_val), IsOk());
  ASSERT_THAT(client_socket_->ReadInt32(), IsOkAndValue(Eq(test_val)));
}

TEST_F(VsockConnectionTest, BasicReadWriteMessage) {
  std::string write_str = "Test data";
  std::vector<char> write_data(write_str.begin(), write_str.end());
  std::vector<char> read_data;
  client_socket_->WriteMessage(write_data);
  server_socket_->ReadMessage(read_data);
  ASSERT_EQ(write_data, read_data);

  std::reverse(write_data.begin(), write_data.end());
  server_socket_->WriteMessage(write_data);
  client_socket_->ReadMessage(read_data);
  ASSERT_EQ(write_data, read_data);
}

TEST_F(VsockConnectionTest, DisconnectClientClientIOFails) {
  client_socket_->Disconnect();
  std::vector<char> data = {1, 2, 3, 4};
  ASSERT_THAT(client_socket_->Read(data), IsError());
  ASSERT_THAT(client_socket_->Write(data), IsError());
  ASSERT_FALSE(client_socket_->IsConnected());
}

TEST_F(VsockConnectionTest, DisconnectClientServerIOFails) {
  client_socket_->Disconnect();
  std::vector<char> data = {1, 2, 3, 4};
  ASSERT_THAT(server_socket_->Read(data), IsError());
  ASSERT_THAT(server_socket_->Write(data), IsError());
  /* Failed reads/writes should have disconnected server */
  ASSERT_FALSE(server_socket_->IsConnected());
  ASSERT_FALSE(client_socket_->IsConnected());
}

TEST_F(VsockConnectionTest, DisconnectServerClientIOFails) {
  server_socket_->Disconnect();
  std::vector<char> data = {1, 2, 3, 4};
  ASSERT_THAT(client_socket_->Read(data), IsError());
  ASSERT_THAT(client_socket_->Write(data), IsError());
  /* Failed reads/writes should have disconnected client */
  ASSERT_FALSE(client_socket_->IsConnected());
  ASSERT_FALSE(server_socket_->IsConnected());
}

TEST_F(VsockConnectionTest, DisconnectServerServerIOFails) {
  server_socket_->Disconnect();
  std::vector<char> data = {1, 2, 3, 4};
  ASSERT_THAT(server_socket_->Read(data), IsError());
  ASSERT_THAT(server_socket_->Write(data), IsError());
  ASSERT_FALSE(server_socket_->IsConnected());
}

TEST_F(VsockConnectionTest, DataAvailablePartialRead) {
  std::vector<char> data = {1, 2, 3, 4};
  ASSERT_THAT(server_socket_->Write(data), IsOk());

  // Wait for and read the first char
  ASSERT_THAT(client_socket_->Read(1), IsOk());
  // Should have three chars left to read
  ASSERT_TRUE(client_socket_->DataAvailable());
  // Never sent any messages to client
  ASSERT_FALSE(server_socket_->DataAvailable());
}

TEST_F(VsockConnectionTest, DataAvailableInitial) {
  ASSERT_FALSE(server_socket_->DataAvailable());
  ASSERT_FALSE(client_socket_->DataAvailable());
}

TEST_F(VsockConnectionTest, DisconnectCallback) {
  bool disconnect_called = false;
  client_socket_->SetDisconnectCallback(
      [&disconnect_called] { disconnect_called = true; });

  client_socket_->Disconnect();
  ASSERT_TRUE(disconnect_called);
}

TEST_F(VsockConnectionTest, JsonMessage) {
  Json::Value test_json;
  test_json["entry"] = false;
  test_json["entry2"] = "testdata";

  ASSERT_THAT(server_socket_->WriteMessage(test_json), IsOk());
  ASSERT_THAT(client_socket_->ReadJsonMessage(), IsOkAndValue(Eq(test_json)));
}
}  // namespace cuttlefish
