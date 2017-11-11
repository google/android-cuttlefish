/*
 * Copyright (C) 2017 The Android Open Source Project
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
#include "host/libs/usbip/client.h"

#include <glog/logging.h>
#include <iostream>

#include "host/libs/usbip/device.h"
#include "host/libs/usbip/messages.h"

namespace vadb {
namespace usbip {

void Client::BeforeSelect(cvd::SharedFDSet* fd_read) const {
  fd_read->Set(fd_);
}

bool Client::AfterSelect(const cvd::SharedFDSet& fd_read) {
  if (fd_read.IsSet(fd_)) return HandleIncomingMessage();
  return true;
}

// Handle incoming COMMAND.
//
// Read next CMD from client channel.
// Returns false, if connection should be dropped.
bool Client::HandleIncomingMessage() {
  CmdHeader hdr;
  if (!RecvUSBIPMsg(fd_, &hdr)) {
    LOG(ERROR) << "Could not read command header: " << fd_->StrError();
    return false;
  }

  // And the protocol, again.
  switch (hdr.command) {
    case kUsbIpCmdReqSubmit:
      return HandleSubmitCmd(hdr);

    case kUsbIpCmdReqUnlink:
      return HandleUnlinkCmd(hdr);

    default:
      LOG(ERROR) << "Unsupported command requested: " << hdr.command;
      return false;
  }
}

// Handle incoming SUBMIT COMMAND.
//
// Execute command on specified USB device.
// Returns false, if connection should be dropped.
bool Client::HandleSubmitCmd(const CmdHeader& cmd) {
  CmdReqSubmit req;
  if (!RecvUSBIPMsg(fd_, &req)) {
    LOG(ERROR) << "Could not read submit command: " << fd_->StrError();
    return false;
  }

  uint32_t seq_num = cmd.seq_num;

  // Reserve buffer for data in or out.
  std::vector<uint8_t> payload;
  int payload_length = req.transfer_buffer_length;
  payload.resize(payload_length);

  bool is_host_to_device = cmd.direction == kUsbIpDirectionOut;
  // Control requests are quite easy to detect; if setup is all '0's, then we're
  // doing a data transfer, otherwise it's a control transfer.
  // We only check for cmd and type fields here, as combination 0/0 of these
  // fields is already invalid (cmd == GET_STATUS, type = WRITE).
  bool is_control_request = !(req.setup.cmd == 0 && req.setup.type == 0);

  // Find requested device and execute command.
  auto device = pool_.GetDevice({cmd.bus_num, cmd.dev_num});
  if (device) {
    // Read data to be sent to device, if specified.
    if (is_host_to_device && payload_length) {
      int32_t got = 0;
      // Make sure we read everything.
      while (got < payload.size()) {
        auto read =
            fd_->Recv(&payload[got], payload.size() - got, MSG_NOSIGNAL);
        if (fd_->GetErrno() != 0) {
          LOG(ERROR) << "Client disconnected: " << fd_->StrError();
          return false;
        } else if (!read) {
          LOG(ERROR) << "Short read; client likely disconnected.";
          return false;
        }
        got += read;
      }
    }

    // If setup structure of request is initialized then we need to execute
    // control transfer. Otherwise, this is a plain data exchange.
    bool send_success = false;
    if (is_control_request) {
      send_success = device->handle_control_transfer(
          req.setup, req.deadline_interval, std::move(payload),
          [this, seq_num, is_host_to_device](bool is_success,
                                             std::vector<uint8_t> data) {
            HandleAsyncDataReady(seq_num, is_success, is_host_to_device,
                                 std::move(data));
          });
    } else {
      send_success = device->handle_data_transfer(
          cmd.endpoint, is_host_to_device, req.deadline_interval,
          std::move(payload),
          [this, seq_num, is_host_to_device](bool is_success,
                                             std::vector<uint8_t> data) {
            HandleAsyncDataReady(seq_num, is_success, is_host_to_device,
                                 std::move(data));
          });
    }

    // Simply fail if couldn't execute command.
    if (!send_success) {
      HandleAsyncDataReady(seq_num, false, is_host_to_device,
                           std::vector<uint8_t>());
    }
  }
  return true;
}

void Client::HandleAsyncDataReady(uint32_t seq_num, bool is_success,
                                  bool is_host_to_device,
                                  std::vector<uint8_t> data) {
  // Response template.
  // - in header, host doesn't care about anything else except for command type
  //   and sequence number.
  // - in body, report status == !OK unless we completed everything
  //   successfully.
  CmdHeader rephdr{};
  rephdr.command = kUsbIpCmdRepSubmit;
  rephdr.seq_num = seq_num;

  CmdRepSubmit rep{};
  rep.status = is_success ? 0 : 1;
  rep.actual_length = data.size();

  // Data out.
  if (!SendUSBIPMsg(fd_, rephdr)) {
    LOG(ERROR) << "Failed to send response header: " << fd_->StrError();
    return;
  }

  if (!SendUSBIPMsg(fd_, rep)) {
    LOG(ERROR) << "Failed to send response body: " << fd_->StrError();
    return;
  }

  if (!is_host_to_device && data.size() > 0) {
    if (fd_->Send(data.data(), data.size(), MSG_NOSIGNAL) != data.size()) {
      LOG(ERROR) << "Failed to send response payload: " << fd_->StrError();
      return;
    }
  }
}

// Handle incoming UNLINK COMMAND.
//
// Unlink removes command specified via seq_num from a list of commands to be
// executed.
// We don't schedule commands for execution, so technically every UNLINK will
// come in late.
// Returns false, if connection should be dropped.
bool Client::HandleUnlinkCmd(const CmdHeader& cmd) {
  CmdReqUnlink req;
  if (!RecvUSBIPMsg(fd_, &req)) {
    LOG(ERROR) << "Could not read unlink command: " << fd_->StrError();
    return false;
  }
  LOG(INFO) << "Client requested to unlink previously submitted command: "
            << req.seq_num;

  CmdHeader rephdr{};
  rephdr.command = kUsbIpCmdRepUnlink;
  rephdr.seq_num = cmd.seq_num;

  // Technically we do not schedule commands for execution, so we cannot
  // de-queue commands, either. Indicate this by sending status != ok.
  CmdRepUnlink rep;
  rep.status = 1;

  if (!SendUSBIPMsg(fd_, rephdr)) {
    LOG(ERROR) << "Could not send unlink command header: " << fd_->StrError();
    return false;
  }

  if (!SendUSBIPMsg(fd_, rep)) {
    LOG(ERROR) << "Could not send unlink command data: " << fd_->StrError();
    return false;
  }
  return true;
}

}  // namespace usbip
}  // namespace vadb
