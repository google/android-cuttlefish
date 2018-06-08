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

#include <arpa/inet.h>

#include <mutex>

#include "common/vsoc/lib/ril_region_view.h"

namespace vsoc {
namespace ril {

const char* RilRegionView::address_and_prefix_length() const {
  static char buffer[sizeof(data().ipaddr) + 3]{};  // <ipaddr>/dd
  if (buffer[0] == '\0') {
    snprintf(buffer, sizeof(buffer), "%s/%d", data().ipaddr, data().prefixlen);
  }
  return &buffer[0];
}

}  // namespace ril
}  // namespace vsoc
