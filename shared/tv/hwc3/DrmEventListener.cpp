/*
 * Copyright 2022 The Android Open Source Project
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

#include "DrmEventListener.h"

#include <linux/netlink.h>
#include <sys/socket.h>

namespace aidl::android::hardware::graphics::composer3::impl {

std::unique_ptr<DrmEventListener> DrmEventListener::create(
    ::android::base::borrowed_fd drmFd, std::function<void()> callback) {
  std::unique_ptr<DrmEventListener> listener(
      new DrmEventListener(std::move(callback)));

  if (!listener->init(drmFd)) {
    return nullptr;
  }

  return listener;
}

bool DrmEventListener::init(::android::base::borrowed_fd drmFd) {
  mEventFd = ::android::base::unique_fd(
      socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT));
  if (!mEventFd.ok()) {
    ALOGE("Failed to open uevent socket: %s", strerror(errno));
    return false;
  }
  struct sockaddr_nl addr;
  memset(&addr, 0, sizeof(addr));
  addr.nl_family = AF_NETLINK;
  addr.nl_pid = 0;
  addr.nl_groups = 0xFFFFFFFF;

  int ret = bind(mEventFd, (struct sockaddr*)&addr, sizeof(addr));
  if (ret) {
    ALOGE("Failed to bind uevent socket: %s", strerror(errno));
    return false;
  }

  FD_ZERO(&mMonitoredFds);
  FD_SET(drmFd.get(), &mMonitoredFds);
  FD_SET(mEventFd.get(), &mMonitoredFds);
  mMaxMonitoredFd = std::max(drmFd.get(), mEventFd.get());

  mThread = std::thread([this]() { threadLoop(); });

  return true;
}

void DrmEventListener::threadLoop() {
  int ret;
  do {
    ret = select(mMaxMonitoredFd + 1, &mMonitoredFds, NULL, NULL, NULL);
  } while (ret == -1 && errno == EINTR);

  if (!FD_ISSET(mEventFd.get(), &mMonitoredFds)) {
    ALOGE("%s: DrmEventListevener event fd unset?", __FUNCTION__);
    return;
  }

  char buffer[1024];
  while (true) {
    ssize_t ret = read(mEventFd.get(), &buffer, sizeof(buffer));
    if (ret == 0) {
      return;
    } else if (ret < 0) {
      ALOGE("Got error reading uevent %zd", ret);
      return;
    }

    bool drmEvent = false, hotplugEvent = false;
    for (int i = 0; i < ret;) {
      char* event = buffer + i;
      if (strcmp(event, "DEVTYPE=drm_minor")) {
        drmEvent = true;
      } else if (strcmp(event, "HOTPLUG=1")) {
        hotplugEvent = true;
      }

      i += strlen(event) + 1;
    }

    if (drmEvent && hotplugEvent) {
      DEBUG_LOG("DrmEventListener detected hotplug event .");
      mOnEventCallback();
    }
  }
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
