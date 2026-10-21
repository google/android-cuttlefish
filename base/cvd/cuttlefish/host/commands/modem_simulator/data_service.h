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

#include "cuttlefish/host/commands/modem_simulator/modem_service.h"

namespace cuttlefish {

class DataService : public ModemService,
                    public std::enable_shared_from_this<DataService> {
 public:
  DataService(int32_t service_id, ChannelMonitor* channel_monitor,
              ThreadLooper* thread_looper);
  ~DataService() = default;

  DataService(const DataService&) = delete;
  DataService& operator=(const DataService&) = delete;

  void HandleActivateDataCall(const Client& client, const std::string& command);
  void HandleQueryDataCallList(const Client& client);
  void HandlePDPContext(const Client& client, const std::string& command);
  void HandleQueryPDPContextList(const Client& client);
  void HandleEnterDataState(const Client& client, const std::string& command);
  void HandleReadDynamicParam(const Client& client, const std::string& command);

  void onUpdatePhysicalChannelconfigs(int modem_tech, int freq,
                                      int cellBandwidthDownlink);

  // Address, gateway and DNS servers of one IP family, formatted as they
  // appear in +CGCONTRDP. An empty address means the family is not available.
  struct IpParams {
    std::string address_and_prefix;
    std::string gateways;
    std::string dnses;
  };

  struct PDPContext {
    enum CidState { ACTIVE, NO_ACTIVE };

    int cid;
    CidState state;
    std::string conn_types;
    std::string apn;
    // IPv4 parameters, or the IPv6 parameters for an IPV6 context.
    std::string addresses;
    std::string dnses;
    std::string gateways;
    // IPv6 parameters of an IPV4V6 context. Empty for other PDP types and
    // when the mobile network has no IPv6.
    IpParams ipv6;
  };

  // Builds the context set by AT+CGDCONT=<cid>,<pdp_type>,<apn>. <pdp_type>
  // is kept as received (quoted). Without IPv6 parameters every PDP type gets
  // the IPv4 parameters, as before IPv6 support.
  static PDPContext MakePDPContext(int cid, const std::string& pdp_type,
                                   const std::string& apn, const IpParams& ipv4,
                                   const IpParams& ipv6);
  // The +CGDCONT? line of a context.
  static std::string PDPContextLine(const PDPContext& context);
  // The +CGCONTRDP lines of an active context.
  static std::vector<std::string> DynamicParamLines(const PDPContext& context);

 private:
  std::vector<CommandHandler> InitializeCommandHandlers();
  void InitializeServiceState();
  void sendOnePhysChanCfgUpdate(int status, int bandwidth, int rat, int freq,
                                int id);
  void updatePhysicalChannelconfigs(int modem_tech, int freq,
                                    int cellBandwidthDownlink, int count);

  std::vector<PDPContext> pdp_context_;
};

}  // namespace cuttlefish
