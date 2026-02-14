//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <stdint.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <set>

#include <tss2/tss2_esys.h>

namespace cuttlefish {

class EsysLock {
 public:
  ESYS_CONTEXT* operator*() const { return esys_; }

 private:
  EsysLock(ESYS_CONTEXT*, std::unique_lock<std::mutex>);

  ESYS_CONTEXT* esys_;
  std::unique_lock<std::mutex> guard_;

  friend class TpmResourceManager;
};

/**
 * Object slot manager for TPM memory. The TPM can only hold a fixed number of
 * objects at once. Some TPM operations are defined to consume slots either
 * temporarily or until the resource is explicitly unloaded.
 *
 * This implementation is intended for future extension, to track what objects
 * are resident if we run out of space, or implement optimizations like LRU
 * caching to avoid re-loading often-used resources.
 */
class TpmResourceManager {
 public:
  class ObjectSlot {
  public:
    friend class TpmResourceManager;

    ~ObjectSlot();

    ESYS_TR get();
    void set(ESYS_TR resource);
  private:
    ObjectSlot(TpmResourceManager* resource_manager);
    ObjectSlot(TpmResourceManager* resource_manager, ESYS_TR resource);

    TpmResourceManager* resource_manager_;
    ESYS_TR resource_;
  };

  TpmResourceManager(ESYS_CONTEXT* esys);
  ~TpmResourceManager();

  // Returns a wrapped ESYS_CONTEXT* that can be used with Esys calls that also
  // holds a lock. Callers should not hold onto the inner ESYS_CONTEXT* past the
  // lifetime of the lock.
  EsysLock Esys();
  std::shared_ptr<ObjectSlot> ReserveSlot();

 private:
  std::mutex mu_;
  ESYS_CONTEXT* esys_;
  const uint32_t maximum_object_slots_;
  std::atomic<uint32_t> used_slots_;
};

using TpmObjectSlot = std::shared_ptr<TpmResourceManager::ObjectSlot>;

}  // namespace cuttlefish
