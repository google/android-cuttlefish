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

#include "cuttlefish/host/libs/metrics/conversion/guest_conversion.h"

#include <string_view>

#include "fmt/format.h"

#include "cuttlefish/host/libs/config/data_image_policy.h"
#include "cuttlefish/host/libs/config/gpu_mode.h"
#include "cuttlefish/host/libs/metrics/device_event_type.h"
#include "cuttlefish/host/libs/metrics/guest_metrics.h"
#include "external_proto/cf_flags.pb.h"
#include "external_proto/cf_guest.pb.h"
#include "external_proto/cf_metrics_event_v2.pb.h"

namespace cuttlefish {
namespace {

using logs::proto::wireless::android::cuttlefish::events::CuttlefishFlags;
using logs::proto::wireless::android::cuttlefish::events::
    CuttlefishFlags_DataPolicy;
using logs::proto::wireless::android::cuttlefish::events::
    CuttlefishFlags_GpuMode;
using logs::proto::wireless::android::cuttlefish::events::CuttlefishGuest;
using logs::proto::wireless::android::cuttlefish::events::
    CuttlefishGuest_EventType;
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
    case GpuMode::GuestLavapipe:
      return CuttlefishFlags_GpuMode::
          CuttlefishFlags_GpuMode_CUTTLEFISH_FLAGS_GPU_MODE_GUEST_LAVAPIPE;
    case GpuMode::None:
      return CuttlefishFlags_GpuMode::
          CuttlefishFlags_GpuMode_CUTTLEFISH_FLAGS_GPU_MODE_NONE;
  }
}

CuttlefishGuest_EventType ConvertDeviceEventType(DeviceEventType event_type) {
  switch (event_type) {
    case DeviceEventType::DeviceInstantiation:
      return CuttlefishGuest_EventType::
          CuttlefishGuest_EventType_CUTTLEFISH_GUEST_EVENT_TYPE_VM_INSTANTIATION;
    case DeviceEventType::DeviceBootStart:
      return CuttlefishGuest_EventType::
          CuttlefishGuest_EventType_CUTTLEFISH_GUEST_EVENT_TYPE_DEVICE_BOOT_START;
    case DeviceEventType::DeviceBootComplete:
      return CuttlefishGuest_EventType::
          CuttlefishGuest_EventType_CUTTLEFISH_GUEST_EVENT_TYPE_DEVICE_BOOT_COMPLETED;
    case DeviceEventType::DeviceStop:
      return CuttlefishGuest_EventType::
          CuttlefishGuest_EventType_CUTTLEFISH_GUEST_EVENT_TYPE_VM_STOP;
    case DeviceEventType::DeviceBootFailed:
      return CuttlefishGuest_EventType::
          CuttlefishGuest_EventType_CUTTLEFISH_GUEST_EVENT_TYPE_DEVICE_BOOT_FAILED;
  }
}

}  // namespace

void PopulateCuttlefishGuest(MetricsEventV2& metrics_event,
                             const GuestMetrics& guest_metrics,
                             std::string_view session_id) {
  CuttlefishGuest& guest = *metrics_event.add_guest();
  guest.set_event_type(ConvertDeviceEventType(guest_metrics.event_type));
  guest.set_guest_id(
      fmt::format("{}-{}", session_id, guest_metrics.instance_id));
  guest.set_guest_os_version(guest_metrics.os_version);

  CuttlefishFlags& flags = *guest.mutable_flags();
  flags.set_cpus(guest_metrics.flag_metrics.cpus);
  flags.set_daemon(guest_metrics.flag_metrics.daemon);
  flags.set_data_policy(
      ConvertDataPolicy(guest_metrics.flag_metrics.data_policy));
  flags.set_extra_kernel_cmdline(
      guest_metrics.flag_metrics.extra_kernel_cmdline);
  flags.set_gpu_mode_requested(
      ConvertGpuMode(guest_metrics.flag_metrics.gpu_mode));
  flags.set_guest_enforce_security(
      guest_metrics.flag_metrics.guest_enforce_security);
  flags.set_memory_mb(guest_metrics.flag_metrics.memory_mb);
  flags.set_restart_subprocesses(
      guest_metrics.flag_metrics.restart_subprocesses);
  flags.set_system_image_dir_specified(
      guest_metrics.flag_metrics.system_image_dir_specified);
}

}  // namespace cuttlefish
