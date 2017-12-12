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
#include "host/vadb/usbip/client.h"

#include <glog/logging.h>
#include <iostream>

#include "host/vadb/usbip/device.h"
#include "host/vadb/usbip/messages.h"

namespace vadb {
namespace usbip {
namespace {
// Parse BUI ID (typically in form #-#) and extract root hub and bus.
// We use these values as synonyms to bus and device numbers internally.
// Returns false, if extracting BusDevNumber was unsuccessful.
bool ParseBusID(const OpReqRepBusId& busid, DevicePool::BusDevNumber* dn) {
  return sscanf(busid, "%hu-%hu", &dn->bus_number, &dn->dev_number) == 2;
}

// Build USBIP Device Report.
void BuildDeviceNode(DevicePool::BusDevNumber dn, const Device& dd,
                     OpRepDeviceInfo* node) {
  memset(node, 0, sizeof(*node));

  snprintf(node->usb_path, sizeof(node->usb_path),
           "/sys/devices/usb/vhci/%hu-%hu", dn.bus_number, dn.dev_number);
  snprintf(node->bus_id, sizeof(node->bus_id), "%hu-%hu", dn.bus_number,
           dn.dev_number);

  node->bus_num = dn.bus_number;
  node->dev_num = dn.dev_number;

  // TODO(ender): How is this defined?
  node->speed = 2;

  node->id_vendor = dd.vendor_id;
  node->id_product = dd.product_id;
  node->bcd_device = dd.dev_version;
  node->device_class = dd.dev_class;
  node->device_subclass = dd.dev_subclass;
  node->device_protocol = dd.dev_protocol;
  node->configuration_value = dd.configuration_number;
  node->num_configurations = dd.configurations_count;
  node->num_interfaces = dd.interfaces.size();
}
}  // namespace

// Handle incoming USB/IP message.
// USB/IP messages have two forms:
// - OPs (OPERATIONs) - are executed only before remote device is attached,
// - CMDs (COMMANDs)  - are executed only after remote device is attached.
// The two types of commands are incompatible with one another, so it's
// impossible to tell which is being parsed, unless you know the state of this
// connection.
//
// Returns false, if connection should be dropped.
bool Client::HandleIncomingMessage() {
  return attached_ ? HandleCommand() : HandleOperation();
}

// Handle incoming OPERATION.
//
// Reads next OP from client channel.
// Returns false, if connection should be dropped.
bool Client::HandleOperation() {
  OpHeader hdr;
  if (!RecvUSBIPMsg(fd_, &hdr)) {
    LOG(ERROR) << "Could not read operation header: " << fd_->StrError();
    return false;
  }

  if (hdr.status != 0) {
    // This really shouldn't happen unless we're already reading random bytes.
    LOG(ERROR) << "Unexpected request status: " << hdr.status;
    return false;
  }

  // USB/IP version is client-driven. Client requires server to support the
  // version reported by client, so we need to cache it somehow.
  if (!proto_version_) {
    proto_version_ = hdr.version;
    if ((proto_version_ < kMinVersion) || (proto_version_ > kMaxVersion)) {
      LOG(ERROR) << "Unsupported USB/IP protocol version: " << proto_version_
                 << ", want: [" << kMinVersion << "-" << kMaxVersion << "].";
      return false;
    }
  } else {
    // Now that we cache client version, we can use it to verify if we're not
    // reading random data bytes again. Sort-of like a MAGIC word.
    if (proto_version_ != hdr.version) {
      LOG(ERROR) << "Inconsistent USB/IP version support reported by client; "
                 << "I've seen " << proto_version_
                 << ", and now i see: " << hdr.version
                 << ". Client is not sane. Disconnecting.";
      return false;
    }
  }

  // Protocol itself. Behold.
  switch (hdr.command) {
    case kUsbIpOpReqDevList:
      return HandleListOp();

    case kUsbIpOpReqImport:
      return HandleImportOp();

    default:
      LOG(WARNING) << "Ignoring unknown command: " << hdr.command;
      // Drop connection at this point. Client may attempt to send some request
      // data after header, and we risk interpreting this as another OP.
      return false;
  }
}

// Handle incoming DEVICE LIST OPERATION.
//
// Send list of (virtual) devices attached to this USB/IP server.
// Returns false, if connection should be dropped.
bool Client::HandleListOp() {
  LOG(INFO) << "Client requests device list";
  // NOTE: Device list Request is currently empty. Do not attempt to read.

  // Send command header
  OpHeader op{};
  op.version = proto_version_;
  op.command = kUsbIpOpRepDevList;
  op.status = 0;
  if (!SendUSBIPMsg(fd_, op)) {
    LOG(ERROR) << "Could not send device list header: " << fd_->StrError();
    return false;
  }

  // Send devlist header
  OpRepDeviceListInfo rep{};
  rep.num_exported_devices = pool_.Size();
  if (!SendUSBIPMsg(fd_, rep)) {
    LOG(ERROR) << "Could not send device list header: " << fd_->StrError();
    return false;
  }

  // Send device reports.
  for (const auto& pair : pool_) {
    OpRepDeviceInfo device;
    BuildDeviceNode(pair.first, *pair.second, &device);
    if (!SendUSBIPMsg(fd_, device)) {
      LOG(ERROR) << "Could not send device list node: " << fd_->StrError();
      return false;
    }

    OpRepInterfaceInfo repif;
    // Interfaces are ligth. Copying value is easier than dereferencing
    // reference.
    for (auto iface : pair.second->interfaces) {
      repif.iface_class = iface.iface_class;
      repif.iface_subclass = iface.iface_subclass;
      repif.iface_protocol = iface.iface_protocol;
      if (!SendUSBIPMsg(fd_, repif)) {
        LOG(ERROR) << "Could not send device list interface: "
                   << fd_->StrError();
        return false;
      }
    }
  }

  LOG(INFO) << "Device list sent.";
  return true;
}

// Handle incoming IMPORT OPERATION.
//
// Attach device to remote host. Flip internal state machine to start processing
// COMMANDs.
// Returns false, if connection should be dropped.
bool Client::HandleImportOp() {
  // Request contains BUS ID
  OpReqRepBusId req;
  if (!RecvUSBIPMsg(fd_, &req)) {
    LOG(ERROR) << "Could not read op import data: " << fd_->StrError();
    return false;
  }
  LOG(INFO) << "Client requests device import for bus" << req;

  // Craft response header.
  OpHeader op{};
  op.version = proto_version_;
  op.command = kUsbIpOpRepImport;
  op.status = 0;

  Device* device = nullptr;
  DevicePool::BusDevNumber dn;

  // Find requested device.
  if (ParseBusID(req, &dn)) {
    device = pool_.GetDevice(dn);
    if (!device || !device->handle_attach()) {
      op.status = 1;
      LOG(ERROR) << "Import failed; No device registered on bus " << req;
    }
  } else {
    LOG(ERROR) << "Could not parse BUS ID: " << req;
    op.status = 1;
  }

  // Craft response data, if device was found.
  OpRepDeviceInfo rep{};
  if (device) {
    BuildDeviceNode(dn, *device, &rep);
  }

  // Send response header.
  if (!SendUSBIPMsg(fd_, op)) {
    LOG(ERROR) << "Could not send import header: " << fd_->StrError();
    return false;
  }

  // Send response data, if header indicates success.
  if (!op.status) {
    if (!SendUSBIPMsg(fd_, rep)) {
      LOG(ERROR) << "Could not send import body: " << fd_->StrError();
      return false;
    }
    attached_ = true;
    LOG(INFO) << "Virtual USB attach successful.";
  }

  return true;
}

// Handle incoming COMMAND.
//
// Read next CMD from client channel.
// Returns false, if connection should be dropped.
bool Client::HandleCommand() {
  CmdHeader hdr;
  if (!RecvUSBIPMsg(fd_, &hdr)) {
    LOG(ERROR) << "Could not read command header: " << fd_->StrError();
    return false;
  }

  // And the protocol, again.
  switch (hdr.command) {
    case kUsbIpCmdReqSubmit:
      HandleSubmitCmd(hdr);
      break;

    case kUsbIpCmdReqUnlink:
      HandleUnlinkCmd(hdr);
      break;

    default:
      LOG(ERROR) << "Unsupported command requested: " << hdr.command;
      return false;
  }
  return true;
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

  // Response template.
  // - in header, host doesn't care about anything else except for command type
  //   and sequence number.
  // - in body, report status == !OK unless we completed everything
  //   successfully.
  CmdHeader rephdr{};
  rephdr.command = kUsbIpCmdRepSubmit;
  rephdr.seq_num = cmd.seq_num;
  CmdRepSubmit rep{};
  rep.status = 1;

  std::vector<uint8_t> payload_in;
  std::vector<uint8_t> payload_out;
  int payload_length = req.transfer_buffer_length;

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
      LOG(INFO) << "Reading payload (" << payload_length << " bytes).";
      payload_in.resize(payload_length);
      auto read = fd_->Recv(payload_in.data(), payload_in.size(), MSG_NOSIGNAL);
      if (read != payload_in.size()) {
        LOG(ERROR) << "Short read while receiving payload; want="
                   << payload_in.size() << ", got=" << read
                   << ", err: " << fd_->StrError();
        return false;
      }
    }

    // If setup structure of request is initialized then we need to execute
    // control transfer. Otherwise, this is a plain data exchange.
    if (is_control_request) {
      rep.status =
          !device->handle_control_transfer(req.setup, payload_in, &payload_out);
    } else {
      rep.status = !device->handle_data_transfer(
          cmd.endpoint, is_host_to_device, req.deadline_interval,
          payload_length, payload_in, &payload_out);
    }
  }

  rep.actual_length =
      is_host_to_device ? payload_in.size() : payload_out.size();

  // Data out.
  if (!SendUSBIPMsg(fd_, rephdr)) {
    LOG(ERROR) << "Failed to send response header: " << fd_->StrError();
    return false;
  }

  if (!SendUSBIPMsg(fd_, rep)) {
    LOG(ERROR) << "Failed to send response body: " << fd_->StrError();
    return false;
  }

  if (payload_out.size()) {
    if (fd_->Send(payload_out.data(), payload_out.size(), MSG_NOSIGNAL) !=
        payload_out.size()) {
      LOG(ERROR) << "Failed to send response payload: " << fd_->StrError();
      return false;
    }
  }

  return true;
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
