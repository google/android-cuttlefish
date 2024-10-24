/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <chrono>
#include <string>

#include <json/json.h>

#include "common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/selector/cvd_persistent_data.pb.h"

namespace cuttlefish {
namespace selector {

class LocalInstance {
 public:
  LocalInstance(const LocalInstance&) = default;
  LocalInstance(LocalInstance&&) = default;
  LocalInstance& operator=(const LocalInstance&) = default;

  uint32_t id() const { return instance_proto_->id(); }
  void set_id(uint32_t id) { instance_proto_->set_id(id); }
  const std::string& name() const { return instance_proto_->name(); }
  cvd::InstanceState state() const { return instance_proto_->state(); }
  void set_state(cvd::InstanceState state);
  const std::string& webrtc_device_id() const {
    return instance_proto_->webrtc_device_id();
  }
  void set_webrtc_device_id(std::string webrtc_device_id) {
    instance_proto_->set_webrtc_device_id(std::move(webrtc_device_id));
  }
  std::string instance_dir() const;
  int adb_port() const;
  const std::string& home_directory() const {
    return group_proto_->home_directory();
  }
  const std::string& host_artifacts_path() const {
    return group_proto_->host_artifacts_path();
  }
  std::string assembly_dir() const;

  bool IsActive() const;
  // Contacts run_cvd to query the instance status. Returns a JSON object with
  // a description of the instance properties. Waits for run_cvd to respond for
  // at most timeout seconds.
  Result<Json::Value> FetchStatus(
      std::chrono::seconds timeout = std::chrono::seconds(5));

 private:
  LocalInstance(std::shared_ptr<cvd::InstanceGroup> group_proto,
                cvd::Instance* instance_proto);

  // Sharing ownership of the group proto ensures the instance proto reference
  // doesn't invalidate.
  std::shared_ptr<cvd::InstanceGroup> group_proto_;
  cvd::Instance* instance_proto_;
  friend class LocalInstanceGroup;
};

}  // namespace selector
}  // namespace cuttlefish
