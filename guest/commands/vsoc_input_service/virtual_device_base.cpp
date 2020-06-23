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

#include "virtual_device_base.h"

#include <android-base/logging.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "log/log.h"

using cuttlefish_input_service::VirtualDeviceBase;

namespace {

bool DoIoctl(int fd, int request, const uint32_t value) {
  int rc = ioctl(fd, request, value);
  if (rc < 0) {
    SLOGE("ioctl failed (%s)", strerror(errno));
    return false;
  }
  return true;
}

}  // namespace

VirtualDeviceBase::VirtualDeviceBase(const char* device_name,
                                     uint16_t product_id)
    : device_name_(device_name),
      bus_type_(BUS_USB),
      vendor_id_(0x6006),
      product_id_(product_id),
      version_(1) {}

VirtualDeviceBase::~VirtualDeviceBase() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

bool VirtualDeviceBase::SetUp() {
  fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (fd_ < 0) {
    SLOGE("Failed to open /dev/uinput (%s)", strerror(errno));
    return false;
  }

  strncpy(dev_.name, device_name_, sizeof(dev_.name));
  dev_.id.bustype = bus_type_;
  dev_.id.vendor = vendor_id_;
  dev_.id.product = product_id_;
  dev_.id.version = version_;

  for (uint32_t evt_type : GetEventTypes()) {
    if (!DoIoctl(fd_, UI_SET_EVBIT, evt_type)) {
      SLOGE("Error setting event type: %" PRIu32, evt_type);
      return false;
    }
  }

  for (uint32_t key : GetKeys()) {
    if (!DoIoctl(fd_, UI_SET_KEYBIT, key)) {
      SLOGE("Error setting key: %" PRIu32, key);
      return false;
    }
  }

  for (uint32_t property : GetProperties()) {
    if (!DoIoctl(fd_, UI_SET_PROPBIT, property)) {
      SLOGE("Error setting property: %" PRIu32, property);
      return false;
    }
  }

  for (uint32_t abs : GetAbs()) {
    if (!DoIoctl(fd_, UI_SET_ABSBIT, abs)) {
      SLOGE("Error setting abs: %" PRIu32, abs);
      return false;
    }
  }

  if (write(fd_, &dev_, sizeof(dev_)) < 0) {
    SLOGE("Unable to set input device info (%s)", strerror(errno));
    return false;
  }
  if (ioctl(fd_, UI_DEV_CREATE) < 0) {
    SLOGE("Unable to create input device (%s)", strerror(errno));
    return false;
  }

  LOG(INFO) << "set up virtual device";

  return true;
}

bool VirtualDeviceBase::EmitEvent(uint16_t type,
                                  uint16_t code,
                                  uint32_t value) {
  struct input_event event {};
  event.type = type;
  event.code = code;
  event.value = value;

  if (write(fd_, &event, sizeof(event)) < static_cast<ssize_t>(sizeof(event))) {
    SLOGE("Event write failed (%s): aborting", strerror(errno));
    return false;
  }
  return true;
}

// By default devices have no event types, keys, properties or absolutes,
// subclasses can override this behavior if necessary.
const std::vector<const uint32_t>& VirtualDeviceBase::GetEventTypes() const {
  static const std::vector<const uint32_t> evt_types{};
  return evt_types;
}
const std::vector<const uint32_t>& VirtualDeviceBase::GetKeys() const {
  static const std::vector<const uint32_t> keys{};
  return keys;
}
const std::vector<const uint32_t>& VirtualDeviceBase::GetProperties() const {
  static const std::vector<const uint32_t> properties{};
  return properties;
}
const std::vector<const uint32_t>& VirtualDeviceBase::GetAbs() const {
  static const std::vector<const uint32_t> abs{};
  return abs;
}
