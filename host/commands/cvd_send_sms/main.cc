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
#include "gflags/gflags.h"

#include "android-base/logging.h"
#include "common/libs/fs/shared_fd.h"
#include "host/commands/cvd_send_sms/sms_sender.h"

DEFINE_string(sender_number, "+16501234567",
              "sender phone number in E.164 format");
DEFINE_uint32(instance_number, 1,
              "number of the cvd instance to send the sms to, default is 1");
DEFINE_uint32(modem_id, 0,
              "modem id needed for multisim devices, default is 0");

namespace cuttlefish {
namespace {

// Usage examples:
//   * cvd_send_sms "hello world"
//   * cvd_send_sms --sender_number="+16501239999" "hello world"
//   * cvd_send_sms --sender_number="16501239999" "hello world"
//   * cvd_send_sms --instance-number=2 "hello world"
//   * cvd_send_sms --instance-number=2 --modem_id=1 "hello world"

int SendSmsMain(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc == 1) {
    LOG(ERROR) << "Missing message content. First positional argument is used "
                  "as the message content, `cvd_send_sms --instance-number=2 "
                  "\"hello world\"`";
    return -1;
  }
  // Builds the name of the corresponding modem simulator monitor socket.
  // https://cs.android.com/android/platform/superproject/+/master:device/google/cuttlefish/host/commands/modem_simulator/main.cpp;l=115;drc=cbfe7dba44bfea95049152b828c1a5d35c9e0522
  std::string socket_name = std::string("modem_simulator") +
                            std::to_string(1000 + FLAGS_instance_number);
  auto client_socket = cuttlefish::SharedFD::SocketLocalClient(
      socket_name.c_str(), /* abstract */ true, SOCK_STREAM);
  SmsSender sms_sender(client_socket);
  if (!sms_sender.Send(argv[1], FLAGS_sender_number, FLAGS_modem_id)) {
    return -1;
  }
  return 0;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) { return cuttlefish::SendSmsMain(argc, argv); }
