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

#pragma once

#include <common/libs/fs/shared_fd.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#ifdef CUTTLEFISH_HOST
#include <host/libs/config/cuttlefish_config.h>
#endif

namespace cuttlefish {

class DeviceConfig {
 public:
  /**
   * WARNING: Consider the possibility of different endianness between host and
   * guest when adding fields of more than one byte to this struct:
   * This struct is meant to be sent from host to guest so the binary
   * representation must be the same. There is a static test that checks for
   * alignment problems, but there is no such thing for endianness.
   */
  struct RawData {
    struct {
      char ipaddr[16];  // xxx.xxx.xxx.xxx\0 = 16 bytes
      char gateway[16];
      char dns[16];
      char broadcast[16];
      uint8_t prefixlen;
      uint8_t reserved[3];
    } ril;
    struct {
      int32_t x_res;
      int32_t y_res;
      int32_t dpi;
      int32_t refresh_rate;
    } screen;
  };

  static std::unique_ptr<DeviceConfig> Get();

  bool SendRawData(SharedFD fd);

  const char* ril_address_and_prefix() const {
    return ril_address_and_prefix_.c_str();
  };
  const char* ril_ipaddr() const { return data_.ril.ipaddr; }
  const char* ril_gateway() const { return data_.ril.gateway; }
  const char* ril_dns() const { return data_.ril.dns; }
  const char* ril_broadcast() const { return data_.ril.broadcast; }
  int ril_prefixlen() const { return data_.ril.prefixlen; }
  int32_t screen_x_res() { return data_.screen.x_res; }
  int32_t screen_y_res() { return data_.screen.y_res; }
  int32_t screen_dpi() { return data_.screen.dpi; }
  int32_t screen_refresh_rate() { return data_.screen.refresh_rate; }

 private:
  void generate_address_and_prefix();
#ifdef CUTTLEFISH_HOST
  DeviceConfig() = default;
  bool InitializeNetworkConfiguration(const CuttlefishConfig& config);
  void InitializeScreenConfiguration(const CuttlefishConfig& config);
#else
  explicit DeviceConfig(const RawData& data);
#endif

  RawData data_;
  std::string ril_address_and_prefix_;
};

}  // namespace cuttlefish
