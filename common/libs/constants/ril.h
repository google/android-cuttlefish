#pragma once
/*
 * Copyright (C) 2019 The Android Open Source Project
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

#if defined(CUTTLEFISH_HOST)
#define CF_PROPERTY_PREFIX "androidboot"
#else
#define CF_PROPERTY_PREFIX "ro.boot"
#endif

#define CUTTLEFISH_RIL_ADDR_PROPERTY CF_PROPERTY_PREFIX ".cuttlefish_ril_addr"
#define CUTTLEFISH_RIL_GATEWAY_PROPERTY \
  CF_PROPERTY_PREFIX ".cuttlefish_ril_gateway"
#define CUTTLEFISH_RIL_DNS_PROPERTY CF_PROPERTY_PREFIX ".cuttlefish_ril_dns"
#define CUTTLEFISH_RIL_BROADCAST_PROPERTY \
  CF_PROPERTY_PREFIX ".cuttlefish_ril_broadcast"
#define CUTTLEFISH_RIL_PREFIXLEN_PROPERTY \
  CF_PROPERTY_PREFIX ".cuttlefish_ril_prefixlen"
