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
#include "host/vadb/usbip/messages.h"

#include <netinet/in.h>
#include <iostream>

#include <glog/logging.h>

namespace vadb {
namespace usbip {
namespace {
// Basic sanity checking.
// We're using CmdHeader + CmdReq/Rep in case any of the fields is moved between
// structures.
constexpr int kUsbIpCmdLength = 48;

static_assert(sizeof(CmdHeader) + sizeof(CmdReqSubmit) == kUsbIpCmdLength,
              "USB/IP command + header must be exactly 48 bytes.");
static_assert(sizeof(CmdHeader) + sizeof(CmdRepSubmit) == kUsbIpCmdLength,
              "USB/IP command + header must be exactly 48 bytes.");
static_assert(sizeof(CmdHeader) + sizeof(CmdReqUnlink) == kUsbIpCmdLength,
              "USB/IP command + header must be exactly 48 bytes.");
static_assert(sizeof(CmdHeader) + sizeof(CmdRepUnlink) == kUsbIpCmdLength,
              "USB/IP command + header must be exactly 48 bytes.");
}  // namespace

// NetToHost and HostToNet are used to reduce risk of copy/paste errors and to
// provide uniform method of converting messages between different endian types.
namespace internal {

template <>
void NetToHost(uint32_t* t) {
  *t = ntohl(*t);
}

template <>
void NetToHost(Command* t) {
  *t = static_cast<Command>(ntohl(*t));
}

template <>
void NetToHost(Direction* t) {
  *t = static_cast<Direction>(ntohl(*t));
}

template <>
void NetToHost(uint16_t* t) {
  *t = ntohs(*t);
}

template <>
void NetToHost(CmdHeader* t) {
  NetToHost(&t->command);
  NetToHost(&t->seq_num);
  NetToHost(&t->bus_num);
  NetToHost(&t->dev_num);
  NetToHost(&t->direction);
  NetToHost(&t->endpoint);
}

template <>
void NetToHost(CmdReqSubmit* t) {
  NetToHost(&t->transfer_flags);
  NetToHost(&t->transfer_buffer_length);
  NetToHost(&t->start_frame);
  NetToHost(&t->number_of_packets);
  NetToHost(&t->deadline_interval);
}

template <>
void NetToHost(CmdReqUnlink* t) {
  NetToHost(&t->seq_num);
}

template <>
void HostToNet(uint32_t* t) {
  *t = htonl(*t);
}

template <>
void HostToNet(Command* t) {
  *t = static_cast<Command>(htonl(*t));
}

template <>
void HostToNet(Direction* t) {
  *t = static_cast<Direction>(htonl(*t));
}

template <>
void HostToNet(uint16_t* t) {
  *t = htons(*t);
}

template <>
void HostToNet(CmdHeader* t) {
  HostToNet(&t->command);
  HostToNet(&t->seq_num);
  HostToNet(&t->bus_num);
  HostToNet(&t->dev_num);
  HostToNet(&t->direction);
  HostToNet(&t->endpoint);
}

template <>
void HostToNet(CmdRepSubmit* t) {
  HostToNet(&t->status);
  HostToNet(&t->actual_length);
  HostToNet(&t->start_frame);
  HostToNet(&t->number_of_packets);
  HostToNet(&t->error_count);
}

template <>
void HostToNet(CmdRepUnlink* t) {
  HostToNet(&t->status);
}

}  // namespace internal

std::ostream& operator<<(std::ostream& out, const CmdHeader& header) {
  out << "CmdHeader\n";
  out << "\t\tcmd:\t" << header.command << '\n';
  out << "\t\tseq#:\t" << header.seq_num << '\n';
  out << "\t\tbus#:\t0x" << header.bus_num << '\n';
  out << "\t\tdev#:\t0x" << header.dev_num << '\n';
  out << "\t\tdir:\t" << (header.direction ? "in" : "out") << '\n';
  out << "\t\tendpt:\t" << header.endpoint << "\n";
  return out;
}

std::ostream& operator<<(std::ostream& out, const CmdRequest& setup) {
  out << "Request\n";
  out << "\t\t\ttype:\t" << std::hex << int(setup.type) << '\n';
  out << "\t\t\treq:\t" << int(setup.cmd) << std::dec << '\n';
  out << "\t\t\tval:\t" << setup.value << '\n';
  out << "\t\t\tidx:\t" << setup.index << '\n';
  out << "\t\t\tlen:\t" << setup.length << '\n';
  return out;
}

std::ostream& operator<<(std::ostream& out, const CmdReqSubmit& submit) {
  out << "CmdReqSubmit\n";
  out << "\t\ttr_flg:\t" << std::hex << submit.transfer_flags << std::dec
      << '\n';
  out << "\t\ttr_len:\t" << submit.transfer_buffer_length << '\n';
  out << "\t\tstart:\t" << submit.start_frame << '\n';
  out << "\t\tpktcnt:\t" << submit.number_of_packets << '\n';
  out << "\t\tttl:\t" << submit.deadline_interval << '\n';
  out << "\t\tsetup:\t" << submit.setup << '\n';
  return out;
}

std::ostream& operator<<(std::ostream& out, const CmdRepSubmit& submit) {
  out << "CmdRepSubmit\n";
  out << "\t\tstatus:\t" << submit.status << '\n';
  out << "\t\tlen:\t" << submit.actual_length << '\n';
  out << "\t\tstart:\t" << submit.start_frame << '\n';
  out << "\t\tpktcnt:\t" << submit.number_of_packets << '\n';
  out << "\t\terrors:\t" << submit.error_count << '\n';
  out << "\t\tsetup:\t" << submit.setup << '\n';
  return out;
}

std::ostream& operator<<(std::ostream& out, const CmdReqUnlink& unlink) {
  out << "CmdReqUnlink\n";
  out << "\t\tseq#:\t" << unlink.seq_num << '\n';
  return out;
}

std::ostream& operator<<(std::ostream& out, const CmdRepUnlink& unlink) {
  out << "CmdRepUnlink\n";
  out << "\t\tstatus:\t" << unlink.status << '\n';
  return out;
}

}  // namespace usbip
}  // namespace vadb
