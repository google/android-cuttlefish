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

#pragma once

#include <json/json.h>

namespace cuttlefish {

// Holds the configuration of modem simulator.
class NvramConfig {

 public:
  static void InitNvramConfigService(size_t num_instances);
  static const NvramConfig* Get();
  static void SaveToFile();

  NvramConfig(size_t num_instances);
  NvramConfig(NvramConfig&&);
  ~NvramConfig();
  NvramConfig& operator=(NvramConfig&&);

  std::string ConfigFileLocation() const;
  // Saves the configuration object in a file
  bool SaveToFile(const std::string& file) const;

  class InstanceSpecific;

  InstanceSpecific ForInstance(int instance_num) const;

  std::vector<InstanceSpecific> Instances() const;

  // A view into an existing modem simulator object for a particular instance.
  class InstanceSpecific {
  public:
    int network_selection_mode() const;
    void set_network_selection_mode(int mode);

    std::string operator_numeric() const;
    void set_operator_numeric(std::string& operator_numeric);

    int modem_technoloy() const;
    void set_modem_technoloy(int technoloy);

    int preferred_network_mode() const;
    void set_preferred_network_mode(int mode);

    bool emergency_mode() const;
    void set_emergency_mode(bool mode);

   private:
    friend InstanceSpecific NvramConfig::ForInstance(int num) const;
    friend std::vector<InstanceSpecific> NvramConfig::Instances() const;

    InstanceSpecific(const NvramConfig* config, const std::string& id)
        : config_(config), id_(id) {}

    Json::Value* Dictionary();
    const Json::Value* Dictionary() const;

    const NvramConfig* config_;
    std::string id_;
  };

 private:
  static std::unique_ptr<NvramConfig> s_nvram_config;
  size_t total_instances_;
  std::unique_ptr<Json::Value> dictionary_;

  bool LoadFromFile(const char* file);
  static NvramConfig* BuildConfigImpl(size_t num_instances);

  void InitDefaultNvramConfig();

  NvramConfig(const NvramConfig&) = delete;
  NvramConfig& operator=(const NvramConfig&) = delete;
};

}  // namespace cuttlefish
