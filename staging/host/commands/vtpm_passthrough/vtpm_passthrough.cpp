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

#include <stdio.h>

#include <android-base/endian.h>
#include <gflags/gflags.h>
#include <android-base/logging.h>

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/logging.h"
#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"

DEFINE_string(device, "", "The device file for the host TPM.");
DEFINE_int32(server_fd, -1, "A server file descriptor to accept guest tpm "
                            "connections.");

namespace {

void HandleClient(cvd::SharedFD client, cvd::SharedFD device) {
  while (true) {
    // TPM2 simulator command protocol.
    std::vector<char> command_bytes(4, 0);
    CHECK(cvd::ReadExact(client, &command_bytes) == 4) << "Could not receive TPM_SEND_COMMAND";
    std::uint32_t command_received =
        betoh32(*reinterpret_cast<std::uint32_t*>(command_bytes.data()));
    CHECK(command_received == 8)
        << "Command received was not TPM_SEND_COMMAND, instead got " << command_received;
    std::vector<char> locality {0};
    CHECK(cvd::ReadExact(client, &locality) == 1) << "Could not receive locality";
    std::vector<char> length_bytes(4, 0);
    CHECK(cvd::ReadExact(client, &length_bytes) == 4) << "Could not receive command length";
    std::vector<char> command(betoh32(*reinterpret_cast<std::uint32_t*>(length_bytes.data())), 0);
    CHECK(cvd::ReadExact(client, &command) == command.size()) << "Could not read TPM message";

    CHECK(device->Write(command.data(), command.size()) == command.size())
        << "Could not write TPM command to host device: " << device->StrError();

    std::string tpm_response;
    CHECK(cvd::ReadAll(device, &tpm_response) >= 0)
        << "host TPM gave an IO error: " << device->StrError();

    *reinterpret_cast<std::uint32_t*>(length_bytes.data()) = htobe32(tpm_response.size());
    CHECK(cvd::WriteAll(client, length_bytes) == 4)
        << "Could not send response length: " << client->StrError();
    CHECK(cvd::WriteAll(client, tpm_response) == tpm_response.size())
        << "Could not send response message: " << client->StrError();
    std::vector<char> parity = {0, 0, 0, 0};
    CHECK(cvd::WriteAll(client, parity) == 4)
        << "Could not send parity bytes: " << client->StrError();
  }
}

} // namespace

int main(int argc, char** argv) {
  cvd::DefaultSubprocessLogging(argv);
  google::ParseCommandLineFlags(&argc, &argv, true);

  CHECK(!FLAGS_device.empty()) << "A device must be set.";
  CHECK(FLAGS_server_fd > -1) << "A server fd must be given.";

  auto server = cvd::SharedFD::Dup(FLAGS_server_fd);
  close(FLAGS_server_fd);
  CHECK(server->IsOpen()) << "Could not dup vsock server fd: " << server->StrError();

  auto device = cvd::SharedFD::Open(FLAGS_device.c_str(), O_RDWR);
  CHECK(device->IsOpen()) << "Could not open " << FLAGS_device << ": " << device->StrError();

  while (true) {
    auto client = cvd::SharedFD::Accept(*server);
    CHECK(client->IsOpen()) << "Could not accept TPM client: " << client->StrError();
    HandleClient(client, device);
  }
}
