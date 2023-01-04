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
#include "host/commands/cvd_send_sms/sms_sender.h"

#include <algorithm>
#include <codecvt>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <map>
#include <vector>

#include "android-base/logging.h"
#include "common/libs/fs/shared_buf.h"
#include "host/commands/cvd_send_sms/pdu_format_builder.h"

namespace cuttlefish {

SmsSender::SmsSender(SharedFD modem_simulator_client_fd)
    : modem_simulator_client_fd_(modem_simulator_client_fd) {}

bool SmsSender::Send(const std::string& content,
                     const std::string& sender_number, uint32_t modem_id) {
  if (!modem_simulator_client_fd_->IsOpen()) {
    LOG(ERROR) << "Failed to connect to remote modem simulator, error: "
               << modem_simulator_client_fd_->StrError();
    return false;
  }
  PDUFormatBuilder builder;
  builder.SetUserData(content);
  builder.SetSenderNumber(sender_number);
  std::string pdu_format_str = builder.Build();
  if (pdu_format_str.empty()) {
    return false;
  }
  // https://cs.android.com/android/platform/superproject/+/master:device/google/cuttlefish/host/commands/modem_simulator/main.cpp;l=151;drc=cbfe7dba44bfea95049152b828c1a5d35c9e0522
  std::string at_command = "REM" + std::to_string(modem_id) +
                           "AT+REMOTESMS=" + pdu_format_str + "\r";
  if (WriteAll(modem_simulator_client_fd_, at_command) != at_command.size()) {
    LOG(ERROR) << "Error writing to socket: "
               << modem_simulator_client_fd_->StrError();
    return false;
  }
  return true;
}
}  // namespace cuttlefish
