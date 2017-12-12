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

#include <errno.h>
#include <string.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include <glog/logging.h>
#include <fstream>
#include <sstream>
#include "common/libs/fs/shared_select.h"

#include "common/libs/fs/shared_fd.h"
#include "host/vadb/usbip/vhci_instrument.h"

namespace vadb {
namespace usbip {
namespace {
// Device ID is specified as a concatenated pair of BUS and DEVICE id.
// Since we only export one device and our server doesn't care much about
// its number, we use the default value of BUS=1 and DEVICE=1.
// This can be set to something else and should still work, as long as
// numbers are valid in USB sense.
constexpr uint32_t kDefaultDeviceID = (1 << 16) | 1;

// Request Highspeed configuration. Superspeed isn't supported by vhci.
// Supported configurations are:
//  4 -> wireless
//  3 -> highspeed
//  2 -> full speed
//  1 -> low speed
//  Please refer to the Kernel source tree in the following locations:
//     include/uapi/linux/usb/ch9.h
//     drivers/usb/usbip/vhci_sysfs.c
constexpr uint32_t kDefaultDeviceSpeed = 3;

// Subsystem and device type where VHCI driver is located.
const char* const kVHCIPlatformPaths[] = {
  "/sys/devices/platform/vhci_hcd",
  "/sys/devices/platform/vhci_hcd.1",
};

// Control messages.
// Attach tells thread to attach remote device.
// Detach tells thread to detach remote device.
using ControlMsgType = uint8_t;
constexpr ControlMsgType kControlAttach = 'A';
constexpr ControlMsgType kControlDetach = 'D';
constexpr ControlMsgType kControlExit = 'E';

// Used with EPOLL as epoll_data to determine event type.
enum EpollEventType {
  kControlEvent,
  kVHCIEvent,
};

// Port status values deducted from /sys/devices/platform/vhci_hcd/status
enum {
  // kVHCIPortFree indicates the port is not currently in use.
  kVHCIStatusPortFree = 4
};
}  // anonymous namespace

VHCIInstrument::VHCIInstrument(const std::string& name)
    : name_(name) {}

VHCIInstrument::~VHCIInstrument() {
  control_write_end_->Write(&kControlExit, sizeof(kControlExit));
  attach_thread_->join();
}

bool VHCIInstrument::Init() {
  avd::SharedFD::Pipe(&control_read_end_, &control_write_end_);

  struct stat buf;
  for (const auto* path : kVHCIPlatformPaths) {
    if (stat(path, &buf) == 0) {
      syspath_ = path;
      break;
    }
  }

  if (syspath_.empty()) {
    LOG(ERROR) << "VHCI not available. Is the driver loaded?";
    LOG(ERROR) << "Try: sudo modprobe vhci_hcd";
    LOG(ERROR) << "The driver is part of linux-image-extra-`uname -r` package";
    return false;
  }

  if (!FindFreePort()) {
    LOG(ERROR) << "It appears all your VHCI ports are currently occupied.";
    LOG(ERROR) << "New VHCI device cannot be registered unless one of the "
               << "ports is freed.";
    return false;
  }

  attach_thread_.reset(new std::thread([this]() { AttachThread(); }));
  return true;
}

bool VHCIInstrument::FindFreePort() {
  std::ifstream stat(syspath_ + "/status");
  int port;
  int status;
  std::string everything_else;

  if (!stat.is_open()) {
    LOG(ERROR) << "Could not open usb-ip status file.";
    return false;
  }

  // Skip past the header line.
  std::getline(stat, everything_else);

  while (stat.rdstate() == std::ios_base::goodbit) {
    stat >> port >> status;
    std::getline(stat, everything_else);
    if (status == kVHCIStatusPortFree) {
      port_ = port;
      LOG(INFO) << "Using VHCI port " << port_;
      return true;
    }
  }
  return false;
}

void VHCIInstrument::TriggerAttach() {
  control_write_end_->Write(&kControlAttach, sizeof(kControlAttach));
}

void VHCIInstrument::TriggerDetach() {
  control_write_end_->Write(&kControlDetach, sizeof(kControlDetach));
}

void VHCIInstrument::AttachThread() {
  avd::SharedFD epoll = avd::SharedFD::Epoll();
  // Trigger attach upon start.
  bool want_attach = true;
  // Operation is pending on read.
  bool is_pending = false;

  epoll_event control_event;
  control_event.events = EPOLLIN;
  control_event.data.u64 = kControlEvent;
  epoll_event vhci_event;
  vhci_event.events = EPOLLRDHUP | EPOLLONESHOT;
  vhci_event.data.u64 = kVHCIEvent;

  epoll->EpollCtl(EPOLL_CTL_ADD, control_read_end_, &control_event);
  while (true) {
    if (vhci_socket_->IsOpen()) {
      epoll->EpollCtl(EPOLL_CTL_ADD, vhci_socket_, &vhci_event);
    }

    epoll_event found_event{};
    ControlMsgType request_type;

    if (epoll->EpollWait(&found_event, 1, 1000)) {
      switch (found_event.data.u64) {
        case kControlEvent:
          control_read_end_->Read(&request_type, sizeof(request_type));
          is_pending = true;
          want_attach = request_type == kControlAttach;
          LOG(INFO) << (want_attach ? "Attach" : "Detach") << " triggered.";
          break;
        case kVHCIEvent:
          vhci_socket_ = avd::SharedFD();
          // Only re-establish VHCI if it was already established before.
          is_pending = want_attach;
          // Do not immediately fall into attach cycle. It will likely complete
          // before VHCI finishes deregistering this callback.
          continue;
      }
    }

    // Make an attempt to re-attach. If successful, clear pending attach flag.
    if (is_pending) {
      if (want_attach && Attach()) {
        is_pending = false;
      } else if (!want_attach && Detach()) {
        is_pending = false;
      } else {
        LOG(INFO) << (want_attach ? "Attach" : "Detach") << " unsuccessful. "
                  << "Will re-try.";
        sleep(1);
      }
    }
  }
}

bool VHCIInstrument::Detach() {
  std::stringstream result;
  result << port_;
  std::ofstream detach(syspath_ + "/detach");

  if (!detach.is_open()) {
    LOG(WARNING) << "Could not open VHCI detach file.";
    return false;
  }
  detach << result.str();
  return detach.rdstate() == std::ios_base::goodbit;
}

bool VHCIInstrument::Attach() {
  if (!vhci_socket_->IsOpen()) {
    vhci_socket_ =
        avd::SharedFD::SocketLocalClient(name_.c_str(), true, SOCK_STREAM);
    if (!vhci_socket_->IsOpen()) return false;
  }

  int sys_fd = vhci_socket_->UNMANAGED_Dup();
  bool success = false;

  {
    std::stringstream result;
    result << port_ << ' ' << sys_fd << ' ' << kDefaultDeviceID << ' '
           << kDefaultDeviceSpeed;
    std::string path = syspath_ + "/attach";
    std::ofstream attach(path);

    if (!attach.is_open()) {
      LOG(WARNING) << "Could not open VHCI attach file " << path << " ("
                   << strerror(errno) << ")";
      close(sys_fd);
      return false;
    }
    attach << result.str();

    // It is unclear whether duplicate FD should remain open or not. There are
    // cases supporting both assumptions, likely related to kernel version.
    // Kernel 4.10 is having problems communicating with USB/IP server if the
    // socket is closed after it's passed to kernel. It is a clear indication that
    // the kernel requires the socket to be kept open.
    success = attach.rdstate() == std::ios_base::goodbit;
    // Make sure everything was written and flushed. This happens when we close
    // the ofstream attach.
  }

  close(sys_fd);
  return success;
}

}  // namespace usbip
}  // namespace vadb
