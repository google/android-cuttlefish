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

#include "cuttlefish/metrics/conversion/fetch_conversion.h"

#include <variant>

#include "cuttlefish/host/commands/cvd/fetch/builds.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/metrics/external_proto/cf_fetch_build.pb.h"
#include "cuttlefish/metrics/external_proto/cf_fetch_builds.pb.h"
#include "cuttlefish/metrics/external_proto/cf_fetch_complete.pb.h"
#include "cuttlefish/metrics/external_proto/cf_fetch_failure.pb.h"
#include "cuttlefish/metrics/external_proto/cf_fetch_start.pb.h"
#include "cuttlefish/metrics/external_proto/cf_metrics_event_v2.pb.h"
#include "cuttlefish/metrics/fetch_metrics.h"

namespace cuttlefish {
namespace {

using logs::proto::wireless::android::cuttlefish::events::CuttlefishBuild;
using logs::proto::wireless::android::cuttlefish::events::CuttlefishFetchBuilds;
using logs::proto::wireless::android::cuttlefish::events::
    CuttlefishFetchComplete;
using logs::proto::wireless::android::cuttlefish::events::
    CuttlefishFetchFailure;
using logs::proto::wireless::android::cuttlefish::events::CuttlefishFetchStart;
using logs::proto::wireless::android::cuttlefish::events::MetricsEventV2;

void PopulateCuttlefishBuild(CuttlefishBuild& cf_build, const Build& build) {
  cf_build.set_build_id(std::visit([](auto&& arg) { return arg.id; }, build));
  cf_build.set_target(std::visit([](auto&& arg) { return arg.target; }, build));
  cf_build.set_product(
      std::visit([](auto&& arg) { return arg.product; }, build));
  if (const DeviceBuild* device_build = std::get_if<DeviceBuild>(&build)) {
    cf_build.set_branch(device_build->branch);
  }
}

void PopulateFetchBuilds(CuttlefishFetchBuilds& fetch_builds,
                         const Builds& builds) {
  if (builds.default_build) {
    CuttlefishBuild& base_build = *fetch_builds.mutable_base();
    PopulateCuttlefishBuild(base_build, builds.default_build.value());
  }
  if (builds.system) {
    CuttlefishBuild& system_build = *fetch_builds.mutable_system();
    PopulateCuttlefishBuild(system_build, builds.system.value());
  }
  if (builds.kernel) {
    CuttlefishBuild& kernel_build = *fetch_builds.mutable_kernel();
    PopulateCuttlefishBuild(kernel_build, builds.kernel.value());
  }
  if (builds.boot) {
    CuttlefishBuild& boot_build = *fetch_builds.mutable_boot();
    PopulateCuttlefishBuild(boot_build, builds.boot.value());
  }
  if (builds.bootloader) {
    CuttlefishBuild& bootloader_build = *fetch_builds.mutable_bootloader();
    PopulateCuttlefishBuild(bootloader_build, builds.bootloader.value());
  }
  if (builds.android_efi_loader) {
    CuttlefishBuild& android_efi_loader_build =
        *fetch_builds.mutable_android_efi_loader();
    PopulateCuttlefishBuild(android_efi_loader_build,
                            builds.android_efi_loader.value());
  }
  if (builds.otatools) {
    CuttlefishBuild& otatools_build = *fetch_builds.mutable_otatools();
    PopulateCuttlefishBuild(otatools_build, builds.otatools.value());
  }
  if (builds.test_suites) {
    CuttlefishBuild& test_suites_build = *fetch_builds.mutable_test_suites();
    PopulateCuttlefishBuild(test_suites_build, builds.test_suites.value());
  }
}

struct PopulateFetchEvent {
  MetricsEventV2& event;

  void operator()(const FetchStartMetrics& start_metrics) {
    CuttlefishFetchStart& start = *event.mutable_start();
    start.set_enable_local_caching(start_metrics.enable_local_caching);
    start.set_dynamic_super_image(start_metrics.dynamic_super_image_mixing);
  }

  void operator()(const FetchCompleteMetrics& complete_metrics) {
    CuttlefishFetchComplete& complete = *event.mutable_complete();
    complete.set_status_blocked(complete_metrics.status_blocked);
    complete.set_fetch_size_bytes(complete_metrics.fetch_size_bytes);
    for (const Builds& builds : complete_metrics.fetched_builds) {
      CuttlefishFetchBuilds& fetch_builds = *complete.add_builds();
      PopulateFetchBuilds(fetch_builds, builds);
    }
  }

  void operator()(const FetchFailedMetrics&) { event.mutable_failure(); }
};

}  // namespace

void PopulateCuttlefishFetch(MetricsEventV2& event,
                             const FetchMetrics& fetch_metrics) {
  std::visit(PopulateFetchEvent{.event = event}, fetch_metrics);
}

}  // namespace cuttlefish
