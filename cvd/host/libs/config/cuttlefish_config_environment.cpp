/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/libs/config/cuttlefish_config.h"

#include "common/libs/utils/files.h"

const char* kEnvironments = "environments";

namespace cuttlefish {

Json::Value* CuttlefishConfig::EnvironmentSpecific::Dictionary() {
  return &(*config_->dictionary_)[kEnvironments][envName_];
}

const Json::Value* CuttlefishConfig::EnvironmentSpecific::Dictionary() const {
  return &(*config_->dictionary_)[kEnvironments][envName_];
}

Json::Value* CuttlefishConfig::MutableEnvironmentSpecific::Dictionary() {
  return &(*config_->dictionary_)[kEnvironments][envName_];
}

std::string CuttlefishConfig::EnvironmentSpecific::environment_name() const {
  return envName_;
}

std::string CuttlefishConfig::EnvironmentSpecific::environment_uds_dir() const {
  return config_->EnvironmentsUdsPath(envName_);
}

std::string CuttlefishConfig::EnvironmentSpecific::PerEnvironmentUdsPath(
    const std::string& file_name) const {
  return (environment_uds_dir() + "/") + file_name;
}

std::string CuttlefishConfig::EnvironmentSpecific::environment_dir() const {
  return config_->EnvironmentsPath(envName_);
}

std::string CuttlefishConfig::EnvironmentSpecific::PerEnvironmentPath(
    const std::string& file_name) const {
  return (environment_dir() + "/") + file_name;
}

std::string CuttlefishConfig::EnvironmentSpecific::PerEnvironmentLogPath(
    const std::string& file_name) const {
  if (file_name.size() == 0) {
    // Don't append a / if file_name is empty.
    return PerEnvironmentPath(kLogDirName);
  }
  auto relative_path = (std::string(kLogDirName) + "/") + file_name;
  return PerEnvironmentPath(relative_path.c_str());
}

std::string CuttlefishConfig::EnvironmentSpecific::PerEnvironmentGrpcSocketPath(
    const std::string& file_name) const {
  if (file_name.size() == 0) {
    // Don't append a / if file_name is empty.
    return PerEnvironmentUdsPath(kGrpcSocketDirName);
  }
  auto relative_path = (std::string(kGrpcSocketDirName) + "/") + file_name;
  return PerEnvironmentUdsPath(relative_path.c_str());
}

std::string CuttlefishConfig::EnvironmentSpecific::control_socket_path() const {
  return PerEnvironmentUdsPath("env_control.sock");
}

std::string CuttlefishConfig::EnvironmentSpecific::launcher_log_path() const {
  return AbsolutePath(PerEnvironmentLogPath("launcher.log"));
}

static constexpr char kEnableWifi[] = "enable_wifi";
void CuttlefishConfig::MutableEnvironmentSpecific::set_enable_wifi(
    bool enable_wifi) {
  (*Dictionary())[kEnableWifi] = enable_wifi;
}
bool CuttlefishConfig::EnvironmentSpecific::enable_wifi() const {
  return (*Dictionary())[kEnableWifi].asBool();
}

static constexpr char kStartWmediumd[] = "start_wmediumd";
void CuttlefishConfig::MutableEnvironmentSpecific::set_start_wmediumd(
    bool start) {
  (*Dictionary())[kStartWmediumd] = start;
}
bool CuttlefishConfig::EnvironmentSpecific::start_wmediumd() const {
  return (*Dictionary())[kStartWmediumd].asBool();
}

static constexpr char kVhostUserMac80211Hwsim[] = "vhost_user_mac80211_hwsim";
void CuttlefishConfig::MutableEnvironmentSpecific::
    set_vhost_user_mac80211_hwsim(const std::string& path) {
  (*Dictionary())[kVhostUserMac80211Hwsim] = path;
}
std::string CuttlefishConfig::EnvironmentSpecific::vhost_user_mac80211_hwsim()
    const {
  return (*Dictionary())[kVhostUserMac80211Hwsim].asString();
}

static constexpr char kWmediumdApiServerSocket[] = "wmediumd_api_server_socket";
void CuttlefishConfig::MutableEnvironmentSpecific::
    set_wmediumd_api_server_socket(const std::string& path) {
  (*Dictionary())[kWmediumdApiServerSocket] = path;
}
std::string CuttlefishConfig::EnvironmentSpecific::wmediumd_api_server_socket()
    const {
  return (*Dictionary())[kWmediumdApiServerSocket].asString();
}

static constexpr char kWmediumdConfig[] = "wmediumd_config";
void CuttlefishConfig::MutableEnvironmentSpecific::set_wmediumd_config(
    const std::string& config) {
  (*Dictionary())[kWmediumdConfig] = config;
}
std::string CuttlefishConfig::EnvironmentSpecific::wmediumd_config() const {
  return (*Dictionary())[kWmediumdConfig].asString();
}

static constexpr char kWmediumdMacPrefix[] = "wmediumd_mac_prefix";
void CuttlefishConfig::MutableEnvironmentSpecific::set_wmediumd_mac_prefix(
    int mac_prefix) {
  (*Dictionary())[kWmediumdMacPrefix] = mac_prefix;
}
int CuttlefishConfig::EnvironmentSpecific::wmediumd_mac_prefix() const {
  return (*Dictionary())[kWmediumdMacPrefix].asInt();
}

}  // namespace cuttlefish
