//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <fstream>
#include <sstream>

#include <json/json.h>
#include <gflags/gflags.h>
#include <android-base/logging.h>

#include "host/libs/config/cuttlefish_config.h"
#include "common/libs/utils/files.h"

#include "nvram_config.h"


namespace cuttlefish {

const char* kInstances            = "instances";
const char* kNetworkSelectionMode = "network_selection_mode";
const char* kOperatorNumeric      = "operator_numeric";
const char* kModemTechnoloy       = "modem_technoloy";
const char* kPreferredNetworkMode = "preferred_network_mode";
const char* kEmergencyMode        = "emergency_mode";

const int   kDefaultNetworkSelectionMode  = 0;     // AUTOMATIC
const std::string kDefaultOperatorNumeric = "";
const int   kDefaultModemTechnoloy        = 0x10;  // LTE
const int   kDefaultPreferredNetworkMode  = 0x13;  // LTE | WCDMA | GSM
const bool  kDefaultEmergencyMode         = false;

/**
 * Creates the (initially empty) config object and populates it with values from
 * the config file "modem_nvram.json" located in the cuttlefish instance path,
 * or uses the default value if the config file not exists,
 * Returns nullptr if there was an error loading from file
 */
NvramConfig* NvramConfig::BuildConfigImpl(size_t num_instances) {
  auto config = cuttlefish::CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();
  auto nvram_config_path = instance.PerInstancePath("modem_nvram.json");

  auto ret = new NvramConfig(num_instances);
  if (ret) {
    if (!cuttlefish::FileExists(nvram_config_path) ||
        !cuttlefish::FileHasContent(nvram_config_path.c_str())) {
      ret->InitDefaultNvramConfig();
    } else {
      auto loaded = ret->LoadFromFile(nvram_config_path.c_str());
      if (!loaded) {
        delete ret;
        return nullptr;
      }
    }
  }
  return ret;
}

std::unique_ptr<NvramConfig> NvramConfig::s_nvram_config;

void NvramConfig::InitNvramConfigService(size_t num_instances) {
  static std::once_flag once_flag;

  std::call_once(once_flag, [num_instances]() {
    NvramConfig::s_nvram_config.reset(BuildConfigImpl(num_instances));
  });
}

/* static */ const NvramConfig* NvramConfig::Get() {
  return s_nvram_config.get();
}

void NvramConfig::SaveToFile() {
  auto nvram_config = Get();
  auto nvram_config_file = nvram_config->ConfigFileLocation();
  nvram_config->SaveToFile(nvram_config_file);
}

NvramConfig::NvramConfig(size_t num_instances)
    : total_instances_(num_instances), dictionary_(new Json::Value()) {}
// Can't use '= default' on the header because the compiler complains of
// Json::Value being an incomplete type
NvramConfig::~NvramConfig() = default;

NvramConfig::NvramConfig(NvramConfig&&) = default;
NvramConfig& NvramConfig::operator=(NvramConfig&&) = default;

NvramConfig::InstanceSpecific NvramConfig::ForInstance(int num) const {
  return InstanceSpecific(this, std::to_string(num));
}

std::string NvramConfig::ConfigFileLocation() const {
  auto cf_instance_num = cuttlefish::CuttlefishConfig::Get();
  auto instance = cf_instance_num->ForDefaultInstance();
  return cuttlefish::AbsolutePath(instance.PerInstancePath("modem_nvram.json"));
}

bool NvramConfig::LoadFromFile(const char* file) {
  auto real_file_path = cuttlefish::AbsolutePath(file);
  if (real_file_path.empty()) {
    LOG(ERROR) << "Could not get real path for file " << file;
    return false;
  }

  Json::Reader reader;
  std::ifstream ifs(real_file_path);
  if (!reader.parse(ifs, *dictionary_)) {
    LOG(ERROR) << "Could not read config file " << file << ": "
               << reader.getFormattedErrorMessages();
    return false;
  }
  return true;
}

bool NvramConfig::SaveToFile(const std::string& file) const {
  std::ofstream ofs(file);
  if (!ofs.is_open()) {
    LOG(ERROR) << "Unable to write to file " << file;
    return false;
  }
  ofs << *dictionary_;
  return !ofs.fail();
}

void NvramConfig::InitDefaultNvramConfig() {
  for (size_t num = 0; num < total_instances_; num++) {
    auto instance = ForInstance(num);
    instance.set_modem_technoloy(kDefaultModemTechnoloy);
    instance.set_network_selection_mode(kDefaultNetworkSelectionMode);
    instance.set_preferred_network_mode(kDefaultPreferredNetworkMode);
    instance.set_emergency_mode(kDefaultEmergencyMode);
  }
}

const Json::Value* NvramConfig::InstanceSpecific::Dictionary() const {
  return &(*config_->dictionary_)[kInstances][id_];
}

Json::Value* NvramConfig::InstanceSpecific::Dictionary() {
  return &(*config_->dictionary_)[kInstances][id_];
}

int NvramConfig::InstanceSpecific::network_selection_mode() const {
  return (*Dictionary())[kNetworkSelectionMode].asInt();
}

void NvramConfig::InstanceSpecific::set_network_selection_mode(int mode) {
  (*Dictionary())[kNetworkSelectionMode] = mode;
}

std::string NvramConfig::InstanceSpecific::operator_numeric() const {
  return (*Dictionary())[kOperatorNumeric].asString();
}

void NvramConfig::InstanceSpecific::set_operator_numeric(std::string& operator_numeric) {
  (*Dictionary())[kOperatorNumeric] = operator_numeric;
}

int NvramConfig::InstanceSpecific::modem_technoloy() const {
  return (*Dictionary())[kModemTechnoloy].asInt();
}

void NvramConfig::InstanceSpecific::set_modem_technoloy(int technoloy) {
  (*Dictionary())[kModemTechnoloy] = technoloy;
}

int NvramConfig::InstanceSpecific::preferred_network_mode() const {
  return (*Dictionary())[kPreferredNetworkMode].asInt();
}

void NvramConfig::InstanceSpecific::set_preferred_network_mode(int mode) {
  (*Dictionary())[kPreferredNetworkMode] = mode;
}

bool NvramConfig::InstanceSpecific::emergency_mode() const {
  return (*Dictionary())[kEmergencyMode].asBool();
}

void NvramConfig::InstanceSpecific::set_emergency_mode(bool mode) {
  (*Dictionary())[kEmergencyMode] = mode;
}

}  // namespace cuttlefish
