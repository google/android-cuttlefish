/*
 * Copyright (C) 2025 The Android Open Source Project
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

#pragma once

#include "cuttlefish/metrics/external_proto/cf_metrics_event_v2.pb.h"
#include "cuttlefish/metrics/host/host_metrics.h"

namespace cuttlefish {
namespace {

using logs::proto::wireless::android::cuttlefish::events::MetricsEventV2;

}  // namespace

void PopulateCuttlefishHost(MetricsEventV2& metrics_event,
                            const HostMetrics& host_metrics);

}  // namespace cuttlefish
