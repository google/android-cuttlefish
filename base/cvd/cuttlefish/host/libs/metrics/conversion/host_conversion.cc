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

#include "cuttlefish/host/libs/metrics/conversion/host_conversion.h"

#include <variant>

#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/host/libs/metrics/host/gce_environment.h"
#include "cuttlefish/host/libs/metrics/host/github_environment.h"
#include "cuttlefish/host/libs/metrics/host/host_metrics.h"
#include "cuttlefish/host/libs/metrics/host/invoker.h"
#include "external_proto/cf_gce_environment.pb.h"
#include "external_proto/cf_github_actions_environment.pb.h"
#include "external_proto/cf_host.pb.h"
#include "external_proto/cf_metrics_event_v2.pb.h"

namespace cuttlefish {
namespace {

using logs::proto::wireless::android::cuttlefish::events::
    CuttlefishGceEnvironment;
using logs::proto::wireless::android::cuttlefish::events::
    CuttlefishGitHubActionsEnvironment;
using logs::proto::wireless::android::cuttlefish::events::
    CuttlefishGitHubActionsEnvironment_Repository;
using logs::proto::wireless::android::cuttlefish::events::CuttlefishHost;
using logs::proto::wireless::android::cuttlefish::events::
    CuttlefishHost_Invoker;
using logs::proto::wireless::android::cuttlefish::events::CuttlefishHost_OsType;
using logs::proto::wireless::android::cuttlefish::events::MetricsEventV2;

CuttlefishHost_OsType ConvertHostOs(const HostInfo& host_info) {
  switch (host_info.os) {
    case Os::Unknown:
      return CuttlefishHost_OsType::
          CuttlefishHost_OsType_CUTTLEFISH_HOST_OS_TYPE_UNSPECIFIED;
    case Os::Linux:
      switch (host_info.arch) {
        case Arch::Arm:
          return CuttlefishHost_OsType::
              CuttlefishHost_OsType_CUTTLEFISH_HOST_OS_TYPE_LINUX_AARCH32;
        case Arch::Arm64:
          return CuttlefishHost_OsType::
              CuttlefishHost_OsType_CUTTLEFISH_HOST_OS_TYPE_LINUX_AARCH64;
        case Arch::RiscV64:
          return CuttlefishHost_OsType::
              CuttlefishHost_OsType_CUTTLEFISH_HOST_OS_TYPE_LINUX_RISCV64;
        case Arch::X86:
          return CuttlefishHost_OsType::
              CuttlefishHost_OsType_CUTTLEFISH_HOST_OS_TYPE_LINUX_X86;
        case Arch::X86_64:
          return CuttlefishHost_OsType::
              CuttlefishHost_OsType_CUTTLEFISH_HOST_OS_TYPE_LINUX_X86_64;
      }
  }
}

CuttlefishHost_Invoker ConvertInvoker(const Invoker invoker) {
  switch (invoker) {
    case Invoker::Acloud:
      return CuttlefishHost_Invoker::
          CuttlefishHost_Invoker_CUTTLEFISH_HOST_INVOKER_ACLOUD;
    case Invoker::HostOrchestrator:
      return CuttlefishHost_Invoker::
          CuttlefishHost_Invoker_CUTTLEFISH_HOST_INVOKER_HOST_ORCHESTRATOR;
    case Invoker::None:
      return CuttlefishHost_Invoker::
          CuttlefishHost_Invoker_CUTTLEFISH_HOST_INVOKER_NONE;
    case Invoker::Unknown:
      return CuttlefishHost_Invoker::
          CuttlefishHost_Invoker_CUTTLEFISH_HOST_INVOKER_UNSPECIFIED;
  }
}

CuttlefishGitHubActionsEnvironment_Repository ConvertGitHubRepository(
    const GitHubRepository repository) {
  switch (repository) {
    case GitHubRepository::AndroidCuttlefish:
      return CuttlefishGitHubActionsEnvironment_Repository::
          CuttlefishGitHubActionsEnvironment_Repository_CUTTLEFISH_GITHUB_ACTIONS_ENVIRONMENT_REPOSITORY_GOOGLE_ANDROID_CUTTLEFISH;
    case GitHubRepository::CloudAndroidOrchestration:
      return CuttlefishGitHubActionsEnvironment_Repository::
          CuttlefishGitHubActionsEnvironment_Repository_CUTTLEFISH_GITHUB_ACTIONS_ENVIRONMENT_REPOSITORY_GOOGLE_CLOUD_ANDROID_ORCHESTRATION;
    case GitHubRepository::Unknown:
      return CuttlefishGitHubActionsEnvironment_Repository::
          CuttlefishGitHubActionsEnvironment_Repository_CUTTLEFISH_GITHUB_ACTIONS_ENVIRONMENT_REPOSITORY_UNSPECIFIED;
  }
}

struct PopulateEnvironment {
  CuttlefishHost& host;

  void operator()(const GceEnvironment& environment) {
    CuttlefishGceEnvironment& gce = *host.mutable_gce();
    gce.set_numeric_project_id(environment.numeric_project_id);
    gce.set_zone(environment.zone);
  }

  void operator()(GitHubRepository repository) {
    CuttlefishGitHubActionsEnvironment& github = *host.mutable_github();
    github.set_repository(ConvertGitHubRepository(repository));
  }

  void operator()(const UnknownEnvironment&) {}
};

void PopulateCuttlefishHostEnvironment(CuttlefishHost& host,
                                       const Environment& environment) {
  std::visit(PopulateEnvironment{.host = host}, environment);
}

}  // namespace

void PopulateCuttlefishHost(MetricsEventV2& metrics_event,
                            const HostMetrics& host_metrics) {
  CuttlefishHost& host = *metrics_event.mutable_host();
  host.set_host_os(ConvertHostOs(host_metrics.os));
  host.set_host_os_version(host_metrics.os.release);
  host.set_invoker(ConvertInvoker(host_metrics.invoker));
  PopulateCuttlefishHostEnvironment(host, host_metrics.environment);
}

}  // namespace cuttlefish
