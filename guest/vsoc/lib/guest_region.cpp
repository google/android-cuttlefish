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
#include "guest/vsoc/lib/guest_region.h"

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <string>
#include <thread>

#include <android-base/logging.h>
#include <uapi/vsoc_shm.h>

using avd::SharedFD;

bool vsoc::OpenableRegion::Open(const char* region_name) {
      std::string path("/dev/");
      path += region_name;
      region_fd_ = SharedFD::Open(path.c_str(), O_RDWR);
      if (!region_fd_->IsOpen()) {
        LOG(FATAL) << "Unable to open region " << region_name << " (" <<
                   region_fd_->StrError() << ")";
        return false;
  }
  if (region_fd_->Ioctl(VSOC_DESCRIBE_REGION, &region_desc_)) {
    LOG(FATAL) << "Unable to obtain region descriptor (" <<
               region_fd_->StrError() << ")";
    return false;
  }
  region_base_ = region_fd_->Mmap(0, region_size(),
                      PROT_READ|PROT_WRITE, MAP_SHARED, 0);
  if (region_base_ == MAP_FAILED) {
    LOG(FATAL) << "mmap failed (" << region_fd_->StrError() << ")";
    return false;
  }
#if LATER
  try_allocate_idx_ = region_offset_to_pointer<std::atomic<uint32_t>>(
      region_desc_.guest_to_host_signal_table.node_alloc_hint_offset);
  std::thread signal_receiver(&Region::ProcessIncomingFutexSignals, this,
                              region_desc_.host_to_guest_signal_table);
  signal_receiver.detach();
#endif
  return true;
}

#if LATER
void vsoc::Region::SignalFutex(std::atomic<uint32_t>* lock_addr) {
  SignalFutexCommon(lock_addr, region_desc_.guest_to_host_signal_table);
}

void vsoc::Region::SendInterruptToPeer() {
  TEMP_FAILURE_RETRY(ioctl(region_fd_, VSOC_SEND_INTERRUPT_TO_HOST, 0));
}

void vsoc::Region::WaitForIncomingInterrupt() {
  TEMP_FAILURE_RETRY(ioctl(region_fd_, VSOC_WAIT_FOR_INCOMING_INTERRUPT, 0));
}
#endif
