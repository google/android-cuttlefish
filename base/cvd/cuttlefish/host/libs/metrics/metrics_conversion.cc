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

#include "cuttlefish/host/libs/metrics/metrics_conversion.h"

#include <chrono>

#include <fmt/format.h>
#include "google/protobuf/timestamp.pb.h"

#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/host/libs/config/data_image_policy.h"
#include "cuttlefish/host/libs/metrics/event_type.h"
#include "external_proto/cf_flags.pb.h"
#include "external_proto/cf_guest.pb.h"
#include "external_proto/cf_host.pb.h"
#include "external_proto/cf_log.pb.h"
#include "external_proto/cf_metrics_event_v2.pb.h"

namespace cuttlefish {
namespace {

using google::protobuf::Timestamp;
using logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent;
using logs::proto::wireless::android::cuttlefish::events::CuttlefishFlags;
using logs::proto::wireless::android::cuttlefish::events::
    CuttlefishFlags_DataPolicy;
using logs::proto::wireless::android::cuttlefish::events::
    CuttlefishFlags_GpuMode;
using logs::proto::wireless::android::cuttlefish::events::CuttlefishGuest;
using logs::proto::wireless::android::cuttlefish::events::
    CuttlefishGuest_EventType;
using logs::proto::wireless::android::cuttlefish::events::CuttlefishHost;
using logs::proto::wireless::android::cuttlefish::events::CuttlefishHost_OsType;
using logs::proto::wireless::android::cuttlefish::events::MetricsEventV2;

CuttlefishFlags_DataPolicy ConvertDataPolicy(DataImagePolicy policy) {
  switch (policy) {
    case DataImagePolicy::AlwaysCreate:
      return CuttlefishFlags_DataPolicy::
          CuttlefishFlags_DataPolicy_CUTTLEFISH_FLAGS_DATA_POLICY_ALWAYS_CREATE;
    case DataImagePolicy::ResizeUpTo:
      return CuttlefishFlags_DataPolicy::
          CuttlefishFlags_DataPolicy_CUTTLEFISH_FLAGS_DATA_POLICY_RESIZE_UP_TO;
    case DataImagePolicy::Unknown:
      return CuttlefishFlags_DataPolicy::
          CuttlefishFlags_DataPolicy_CUTTLEFISH_FLAGS_DATA_POLICY_UNSPECIFIED;
    case DataImagePolicy::UseExisting:
      return CuttlefishFlags_DataPolicy::
          CuttlefishFlags_DataPolicy_CUTTLEFISH_FLAGS_DATA_POLICY_USE_EXISTING;
  }
}

CuttlefishFlags_GpuMode ConvertGpuMode(GpuMode mode) {
  switch (mode) {
    case GpuMode::Auto:
      return CuttlefishFlags_GpuMode::
          CuttlefishFlags_GpuMode_CUTTLEFISH_FLAGS_GPU_MODE_AUTO;
    case GpuMode::Custom:
      return CuttlefishFlags_GpuMode::
          CuttlefishFlags_GpuMode_CUTTLEFISH_FLAGS_GPU_MODE_CUSTOM;
    case GpuMode::DrmVirgl:
      return CuttlefishFlags_GpuMode::
          CuttlefishFlags_GpuMode_CUTTLEFISH_FLAGS_GPU_MODE_GUEST_VIRGL_RENDERER;
    case GpuMode::Gfxstream:
      return CuttlefishFlags_GpuMode::
          CuttlefishFlags_GpuMode_CUTTLEFISH_FLAGS_GPU_MODE_GUEST_GFXSTREAM;
    case GpuMode::GfxstreamGuestAngle:
      return CuttlefishFlags_GpuMode::
          CuttlefishFlags_GpuMode_CUTTLEFISH_FLAGS_GPU_MODE_GUEST_GFXSTREAM_GUEST_ANGLE;
    case GpuMode::GfxstreamGuestAngleHostLavapipe:
      return CuttlefishFlags_GpuMode::
          CuttlefishFlags_GpuMode_CUTTLEFISH_FLAGS_GPU_MODE_GUEST_GFXSTREAM_GUEST_ANGLE_HOST_LAVAPIPE;
    case GpuMode::GfxstreamGuestAngleHostSwiftshader:
      return CuttlefishFlags_GpuMode::
          CuttlefishFlags_GpuMode_CUTTLEFISH_FLAGS_GPU_MODE_GUEST_GFXSTREAM_GUEST_ANGLE_HOST_SWIFTSHADER;
    case GpuMode::GuestSwiftshader:
      return CuttlefishFlags_GpuMode::
          CuttlefishFlags_GpuMode_CUTTLEFISH_FLAGS_GPU_MODE_GUEST_SWIFTSHADER;
    case GpuMode::None:
      return CuttlefishFlags_GpuMode::
          CuttlefishFlags_GpuMode_CUTTLEFISH_FLAGS_GPU_MODE_NONE;
  }
}

CuttlefishGuest_EventType ConvertEventType(EventType event_type) {
  switch (event_type) {
    case EventType::DeviceInstantiation:
      return CuttlefishGuest_EventType::
          CuttlefishGuest_EventType_CUTTLEFISH_GUEST_EVENT_TYPE_VM_INSTANTIATION;
    case EventType::DeviceBootStart:
      return CuttlefishGuest_EventType::
          CuttlefishGuest_EventType_CUTTLEFISH_GUEST_EVENT_TYPE_DEVICE_BOOT_START;
    case EventType::DeviceBootComplete:
      return CuttlefishGuest_EventType::
          CuttlefishGuest_EventType_CUTTLEFISH_GUEST_EVENT_TYPE_DEVICE_BOOT_COMPLETED;
    case EventType::DeviceStop:
      return CuttlefishGuest_EventType::
          CuttlefishGuest_EventType_CUTTLEFISH_GUEST_EVENT_TYPE_VM_STOP;
    case EventType::DeviceBootFailed:
      return CuttlefishGuest_EventType::
          CuttlefishGuest_EventType_CUTTLEFISH_GUEST_EVENT_TYPE_DEVICE_BOOT_FAILED;
  }
}

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

void PopulateCuttlefishGuest(CuttlefishGuest& guest,
                             const GuestMetrics& guest_info,
                             const FlagMetrics& flag_metrics,
                             const EventType event_type,
                             std::string_view session_id) {
  guest.set_event_type(ConvertEventType(event_type));
  guest.set_guest_id(fmt::format("{}-{}", session_id, guest_info.instance_id));
  guest.set_guest_os_version(guest_info.os_version);

  CuttlefishFlags& flags = *guest.mutable_flags();
  flags.set_cpus(flag_metrics.cpus);
  flags.set_daemon(flag_metrics.daemon);
  flags.set_data_policy(ConvertDataPolicy(flag_metrics.data_policy));
  flags.set_extra_kernel_cmdline(flag_metrics.extra_kernel_cmdline);
  flags.set_gpu_mode_requested(ConvertGpuMode(flag_metrics.gpu_mode));
  flags.set_guest_enforce_security(flag_metrics.guest_enforce_security);
  flags.set_memory_mb(flag_metrics.memory_mb);
  flags.set_restart_subprocesses(flag_metrics.restart_subprocesses);
  flags.set_system_image_dir_specified(flag_metrics.system_image_dir_specified);
}

}  // namespace

CuttlefishLogEvent BuildCuttlefishLogEvent(const MetricsData& metrics_data) {
  CuttlefishLogEvent cf_log_event;
  cf_log_event.set_device_type(CuttlefishLogEvent::CUTTLEFISH_DEVICE_TYPE_HOST);
  cf_log_event.set_session_id(metrics_data.session_id);
  cf_log_event.set_cuttlefish_version(metrics_data.cf_common_version);
  Timestamp& timestamp = *cf_log_event.mutable_timestamp_ms();
  timestamp.set_nanos((metrics_data.now.count() % 1000) * 1000000);
  timestamp.set_seconds(metrics_data.now.count() / 1000);

  MetricsEventV2& metrics_event = *cf_log_event.mutable_metrics_event_v2();

  for (int i = 0; i < metrics_data.guest_metrics.size(); i++) {
    CuttlefishGuest& guest = *metrics_event.add_guest();
    PopulateCuttlefishGuest(guest, metrics_data.guest_metrics[i],
                            metrics_data.flag_metrics[i],
                            metrics_data.event_type, metrics_data.session_id);
  }

  CuttlefishHost& host = *metrics_event.mutable_host();
  host.set_host_os(ConvertHostOs(metrics_data.host_metrics));
  host.set_host_os_version(metrics_data.host_metrics.release);

  return cf_log_event;
}

}  // namespace cuttlefish
