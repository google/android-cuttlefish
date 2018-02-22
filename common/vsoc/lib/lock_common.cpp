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
#include "common/vsoc/shm/lock.h"

#include "common/libs/glog/logging.h"
#include "common/vsoc/lib/compat.h"
#include "common/vsoc/lib/region_view.h"

#include <stdlib.h>

using vsoc::layout::Sides;

namespace {

const uint32_t LockFree = 0U;
const uint32_t GuestWaitingFlag = (Sides::Guest << 30);
const uint32_t HostWaitingFlag = (Sides::Host << 30);
const uint32_t OurWaitingFlag = (Sides::OurSide << 30);
static_assert(GuestWaitingFlag, "GuestWaitingFlag is 0");
static_assert(HostWaitingFlag, "HostWaitingFlag is 0");
static_assert((GuestWaitingFlag & HostWaitingFlag) == 0,
              "Waiting flags should not share bits");

// Set if the current owner is the host
const uint32_t HostIsOwner = 0x20000000U;

// PID_MAX_LIMIT appears to be 0x00400000U, so we're probably ok here
const uint32_t OwnerMask = 0x3FFFFFFFU;

uint32_t MakeOwnerTid(uint32_t raw_tid) {
  if (Sides::OurSide == Sides::Host) {
    return (raw_tid | HostIsOwner) & OwnerMask;
  } else {
    return raw_tid & (OwnerMask & ~HostIsOwner);
  }
}

};  // namespace

namespace vsoc {
/**
 * This is a generic synchronization primitive that provides space for the
 * owner of the lock to write platform-specific information.
 */
bool vsoc::layout::WaitingLockBase::TryLock(uint32_t tid,
                                            uint32_t* expected_out) {
  uint32_t masked_tid = MakeOwnerTid(tid);
  uint32_t expected = LockFree;
  while (1) {
    // First try to lock assuming that the mutex is free
    if (lock_uint32_.compare_exchange_strong(expected, masked_tid)) {
      // We got the lock.
      return true;
    }
    // We didn't get the lock and our wait flag is already set. It's safe to
    // try to sleep
    if (expected & OurWaitingFlag) {
      *expected_out = expected;
      return false;
    }
    // Attempt to set the wait flag. This will fail if the lock owner changes.
    while (1) {
      uint32_t add_wait_flag = expected | OurWaitingFlag;
      if (lock_uint32_.compare_exchange_strong(expected, add_wait_flag)) {
        // We set the waiting flag. Try to sleep.
        *expected_out = expected;
        return false;
      }
      // The owner changed, but we at least we got the wait flag.
      // Try sleeping
      if (expected & OurWaitingFlag) {
        *expected_out = expected;
        return false;
      }
      // Special case: the lock was just freed. Stop trying to set the
      // waiting flag and try to grab the lock.
      if (expected == LockFree) {
        break;
      }
      // The owner changed and we have no wait flag
      // Try to set the wait flag again
    }
    // This only happens if the lock was freed while we attempt the set the
    // wait flag. Try to grab the lock again
  }
  // Never reached.
}

layout::Sides vsoc::layout::WaitingLockBase::UnlockCommon(uint32_t tid) {
  uint32_t expected_state = lock_uint32_;

  // We didn't hold the lock. This process is insane and must die before it
  // does damage.
  uint32_t marked_tid = MakeOwnerTid(tid);
  if ((marked_tid ^ expected_state) & OwnerMask) {
    LOG(FATAL) << tid << " unlocking " << this << " owned by "
               << expected_state;
  }
  // If contention is just starting this may fail twice (once for each bit)
  // expected_state updates on each failure. When this finishes we have
  // one bit for each waiter
  while (1) {
    if (lock_uint32_.compare_exchange_strong(expected_state, LockFree)) {
      break;
    }
  }
  if ((expected_state ^ marked_tid) & OwnerMask) {
    LOG(FATAL) << "Lock owner of " << this << " changed from " << tid << " to "
               << expected_state << " during unlock";
  }
  switch (expected_state & (GuestWaitingFlag | HostWaitingFlag)) {
    case 0:
      return Sides::NoSides;
    case GuestWaitingFlag:
      return Sides::Guest;
    case HostWaitingFlag:
      return Sides::Host;
    default:
      return Sides::Both;
  }
}

bool vsoc::layout::WaitingLockBase::RecoverSingleSided() {
  // No need to signal because the caller ensured that there were no other
  // threads...
  return lock_uint32_.exchange(LockFree) != LockFree;
}

void layout::GuestAndHostLock::Lock(RegionView* region) {
  uint32_t expected;
  uint32_t tid = gettid();
  while (1) {
    if (TryLock(tid, &expected)) {
      return;
    }
    region->WaitForSignal(&lock_uint32_, expected);
  }
}

void layout::GuestAndHostLock::Unlock(RegionView* region) {
  region->SendSignal(UnlockCommon(gettid()), &lock_uint32_);
}

bool layout::GuestAndHostLock::Recover(RegionView* region) {
  uint32_t expected_state = lock_uint32_;
  uint32_t expected_owner_bit = (Sides::OurSide == Sides::Host) ? HostIsOwner : 0;
  // This avoids check then act by reading exactly once and then
  // eliminating the states where Recover should be a noop.
  if (expected_state == LockFree) {
    return false;
  }
  // Owned by the other side, do nothing.
  if ((expected_state & HostIsOwner) != expected_owner_bit) {
    return false;
  }
  // At this point we know two things:
  //   * The lock was held by our side
  //   * There are no other threads running on our side (precondition
  //     for calling Recover())
  // Therefore, we know that the current expected value should still
  // be in the lock structure. Use the normal unlock logic, providing
  // the tid that we observed in the lock.
  region->SendSignal(UnlockCommon(expected_state), &lock_uint32_);
  return true;
}

}  // namespace vsoc
