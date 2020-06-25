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

#include "tpm_resource_manager.h"

#include <android-base/logging.h>
#include <tss2/tss2_rc.h>

TpmResourceManager::ObjectSlot::ObjectSlot(TpmResourceManager* resource_manager)
    : ObjectSlot(resource_manager, ESYS_TR_NONE) {
}

TpmResourceManager::ObjectSlot::ObjectSlot(TpmResourceManager* resource_manager,
                                           ESYS_TR resource)
    : resource_manager_(resource_manager), resource_(resource) {
  LOG(VERBOSE) << "Resource allocated";
}

TpmResourceManager::ObjectSlot::~ObjectSlot() {
  if (resource_ != ESYS_TR_NONE) {
    LOG(VERBOSE) << "Freeing resource";
    auto rc = Esys_FlushContext(resource_manager_->esys_, resource_);
    if (rc != TPM2_RC_SUCCESS) {
      LOG(ERROR) << "Esys_FlushContext failed: " << Tss2_RC_Decode(rc)
                << "(" << rc << ")";
    }
  } else {
    LOG(VERBOSE) << "Resource is NONE";
  }
  resource_manager_->used_slots_--;
}

ESYS_TR TpmResourceManager::ObjectSlot::get() {
  return resource_;
}

void TpmResourceManager::ObjectSlot::set(ESYS_TR resource) {
  resource_ = resource;
}

TpmResourceManager::TpmResourceManager(ESYS_CONTEXT* esys)
    : esys_(esys), maximum_object_slots_(3), used_slots_(0) {
  // TODO(b/158791154): Find maximum_object_slots dynamically using
  // TPM2_GetCapability. Now equal to MAX_LOADED_OBJECTS from TpmProfile.h.
}

TpmResourceManager::~TpmResourceManager() {
  if (used_slots_ > 0) {
    LOG(FATAL) << "Outstanding TpmResourceManager::ObjectSlot instances. "
                  "These hold a dangling pointer to this instance.";
  }
}

ESYS_CONTEXT* TpmResourceManager::Esys() {
  return esys_;
}

TpmObjectSlot TpmResourceManager::ReserveSlot() {
  auto slot_num = used_slots_.fetch_add(1);
  if (slot_num >= maximum_object_slots_) {
      used_slots_--;
      return nullptr;
  }
  return TpmObjectSlot{new ObjectSlot(this)};
}
