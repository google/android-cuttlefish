/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "resource.h"

#include <android-base/logging.h>

#include "alloc_utils.h"

namespace cuttlefish {

bool MobileIface::AcquireResource() {
  return CreateMobileIface(GetName(), iface_id_, ipaddr_);
}

bool MobileIface::ReleaseResource() {
  return DestroyMobileIface(GetName(), iface_id_, ipaddr_);
}

bool EthernetIface::AcquireResource() {
  return CreateEthernetIface(GetName(), GetBridgeName(), has_ipv4_, has_ipv6_,
                             use_ebtables_legacy_);
}

bool EthernetIface::ReleaseResource() {
  return DestroyEthernetIface(GetName(), has_ipv4_, has_ipv6_,
                              use_ebtables_legacy_);
}

}  // namespace cuttlefish
