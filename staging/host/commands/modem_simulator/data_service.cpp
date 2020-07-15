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

#include <android-base/strings.h>

#include "common/libs/device_config/device_config.h"

#include "data_service.h"

namespace cuttlefish {

static std::unique_ptr<cuttlefish::DeviceConfig> data_connet_config_ = nullptr;

DataService::DataService(int32_t service_id, ChannelMonitor* channel_monitor,
                         ThreadLooper* thread_looper)
    : ModemService(service_id, this->InitializeCommandHandlers(),
                   channel_monitor, thread_looper) {
  InitializeServiceState();
}

std::vector<CommandHandler> DataService::InitializeCommandHandlers() {
  std::vector<CommandHandler> command_handlers = {
      CommandHandler("+CGACT=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleActivateDataCall(client, cmd);
                     }),
      CommandHandler("+CGACT?",
                     [this](const Client& client) {
                       this->HandleQueryDataCallList(client);
                     }),
      CommandHandler("+CGDCONT=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandlePDPContext(client, cmd);
                     }),
      CommandHandler("+CGDCONT?",
                     [this](const Client& client) {
                       this->HandleQueryPDPContextList(client);
                     }),
      CommandHandler("+CGQREQ=1",
                     [this](const Client& client) {
                       this->HandleCommandDefaultSupported(client);
                     }),
      CommandHandler("+CGQMIN=1",
                     [this](const Client& client) {
                       this->HandleCommandDefaultSupported(client);
                     }),
      CommandHandler("+CGEREP=1,0",
                     [this](const Client& client) {
                       this->HandleCommandDefaultSupported(client);
                     }),
      CommandHandler("+CGDATA",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleEnterDataState(client, cmd);
                     }),
      CommandHandler("D*99***1#",
                     [this](const Client& client) {
                       this->HandleCommandDefaultSupported(client);
                     }),
      CommandHandler("+CGCONTRDP",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleReadDynamicParam(client, cmd);
                     }),
  };
  return (command_handlers);
}

void DataService::InitializeServiceState() {
  // Initialize data connection config
  data_connet_config_ = cuttlefish::DeviceConfig::Get();
  if (!data_connet_config_) {
    LOG(ERROR) << "Failed to open device configuration!";
  }
}

/**
 * AT+CGACT
 *   The execution command is used to activate or deactivate the specified PDP
 * context(s).
 *
 * Command                            Possible response(s)
 * +CGACT=[<state>[,<cid>              OK
 *        [,<cid>[,...]]]]             +CME ERROR: <err>
 * +CGACT?                             [+CGACT: <cid>,<state>]
 *                                     [<CR><LF>+CGACT: <cid>,<state>[...]]
 * <state>: integer type; indicates the state of PDP context activation.
 *       0: deactivated
 *       1: activated
 * <cid>: (PDP Context Identifier) integer(1~15), specifies the PDP context ID.
 *
 * see RIL_REQUEST_SETUP_DATA_CALL in RIL
 */
void DataService::HandleActivateDataCall(const Client& client,
                                         const std::string& /*command*/) {
  client.SendCommandResponse("OK");
}

/**
 * see AT+CGACT
 */
void DataService::HandleQueryDataCallList(const Client& client) {
  std::vector<std::string> responses;

  std::stringstream ss;
  for (auto iter = pdp_context_.begin(); iter != pdp_context_.end(); ++iter) {
    if (iter->state == PDPContext::ACTIVE) {
      ss.clear();
      ss << "+CGACT: " << iter->cid << "," << iter->state;
      responses.push_back(ss.str());
      ss.str("");
    }
  }
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/**
 * AT+CGDCONT
 *   The set command specifies PDP context parameter values for a PDP context
 * identified by the (local) context identification parameter, <cid>.
 *
 * Command                            Possible response(s)
 * +CGDCONT=[<cid>[,<PDP_type>[,<APN>  OK
 * [,<PDP_addr>[,<d_comp> [,<h_comp>]  +CME ERROR: <err>
 * ]]]]]
 * +CGDCONT?                           +CGDCONT: <cid>,<pdp_type>,<APN>,
 *                                     <pdp_addr>,<d_comp>,<h_comp><CR><LF>
 *                                     [+CGDCONT: <cid>,<pdp_type>,<APN>,
 *                                     <pdp_addr>,<d_comp>,<h_comp><CR><LF>[...]]
 *                                     OK
 * <cid>: see AT+CGACT
 * <PDP_type>: string type; specifies the type of packet data protocol.
 *             Value: X.25, IP, IPV6, IPV4V6, OSPIH, PPP, Non-IP,Ethernet
 * <APN>: string type; a logical name that is used to select the GGSN or the
 *        external packet data network.If the value is null or omitted, then
 *        the subscription value will be requested
 * <PDP_addr>: string type; identifies the MT in the address space applicable
 *             to the PDP
 * <d_comp>: integer type; controls PDP data compression
 * <h_comp>: integer type; controls PDP header compression
 *
 * see RIL_REQUEST_SETUP_DATA_CALL in RIL
 */
void DataService::HandlePDPContext(const Client& client,
                                   const std::string& command) {
  CommandParser cmd(command);
  cmd.SkipPrefix(); /* skip +CGDCONT= */
  int cid = cmd.GetNextInt();

  std::string ip_type(cmd.GetNextStr(','));
  std::string apn(cmd.GetNextStr(','));
  std::string address = "10.0.2.15/24";
  std::string dnses = "8.8.8.8";
  std::string gateways = "10.0.2.2";

  if (data_connet_config_) {
    address = data_connet_config_->ril_address_and_prefix();
    dnses = data_connet_config_->ril_dns();
    gateways = data_connet_config_->ril_gateway();
  } else {
    LOG(ERROR) << "Device connect configuration is nullptr !";
  }

  PDPContext pdp_context = {cid,
                            PDPContext::ACTIVE,
                            ip_type,  // IPV4 or IPV6 or IPV4V6
                            apn,
                            address,
                            dnses,
                            gateways};

  // check cid
  auto iter = pdp_context_.begin();
  for (; iter != pdp_context_.end(); ++iter) {
    if (pdp_context.cid == iter->cid) {
      *iter = pdp_context;
      break;
    }
  }

  if (iter == pdp_context_.end()) {
    pdp_context_.push_back(pdp_context);
  }

  client.SendCommandResponse("OK");
}

/**
 * see AT+CGDCONT above
 */
void DataService::HandleQueryPDPContextList(const Client& client) {
  std::vector<std::string> responses;

  std::stringstream ss;
  for (auto it = pdp_context_.begin(); it != pdp_context_.end(); ++it) {
    std::stringstream ss;
    ss << "+CGDCONT: " << it->cid << "," << it->conn_types << ","
       << it->apn << "," << it->addresses << ",0,0";
    responses.push_back(ss.str());
  }
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/**
 * AT+CGDATA
 *   The execution command causes the MT to perform whatever actions are
 * necessary to establish communication between the TE and the network using
 * one or more Packet Domain PDP types.
 *
 * Command                            Possible response(s)
 * +CGDATA[=<L2P>[,[,<cid>             CONNECT
 *          [,...]]]]                  ERROR
 *                                     +CME ERROR: <err>
 *
 * <L2P>: string type; indicates the layer 2 protocol to be used between the
 *        TE and MT NULL  none, for PDP type OSP:IHOSS (Obsolete)
 *        value: PPP, PAD, X25, M-xxxx
 * <cid>: see AT+CGACT
 *
 * see RIL_REQUEST_SETUP_DATA_CALL in RIL
 */
void DataService::HandleEnterDataState(const Client& client,
                                       const std::string& command) {
  std::string response;

  CommandParser cmd(command);
  cmd.SkipPrefix();
  cmd.SkipComma();
  int cid = cmd.GetNextInt();

  // Check cid
  auto iter = pdp_context_.begin();
  for (; iter != pdp_context_.end(); ++iter) {
    if (cid == iter->cid && iter->state == PDPContext::ACTIVE) {
      response = "CONNECT";
      break;
    }
  }

  if (iter == pdp_context_.end()) {
    response = "ERROR";
  }

  client.SendCommandResponse(response);
}

/**
 * AT+CGCONTRDP
 *   The execution command returns the relevant information for an active non
 * secondary PDP context with the context identifier <cid>.
 *
 * Command                            Possible response(s)
 * +CGCONTRDP[=<cid>]                 [+CGCONTRDP: <cid>,<bearer_id>,<apn>
 *                                    [,<local_addr and subnet_mask>[,<gw_addr>
 *                                    [,<DNS_prim_addr>[<DNS_sec_addr>[...]]]]]]
 *                                    [<CR><LF>+CGCONTRDP: <cid>,<bearer_id>,<apn>
 *                                    [,<local_addr and subnet_mask>[,<gw_addr>
 *                                    [,<DNS_prim_addr>[<DNS_sec_addr>[...]]]]]]
 *
 * <cid>: see AT+CGACT
 * <bearer_id>: integer type; identifies the bearer, i.e. the EPS bearer and
 *              the NSAPI.
 * <local_addr and subnet_mask>: string type; shows the IP address and subnet
 *                               mask of the MT.
 * <gw_addr>: string type; shows the Gateway Address of the MT. The string is
 *            given as dot-separated numeric (0-255) parameters.
 * <DNS_prim_addr>: string type; shows the IP address of the primary DNS server.
 * <DNS_sec_addr>: string type; shows the IP address of the secondary DNS server.
 *
 *
 * see RIL_REQUEST_SETUP_DATA_CALL in RIL
 */
void DataService::HandleReadDynamicParam(const Client& client,
                                         const std::string& command) {
  std::vector<std::string> responses;

  CommandParser cmd(command);
  cmd.SkipPrefix(); /* skip prefix AT+CGCONTRDP= */

  int cid = cmd.GetNextInt();
  auto iter = pdp_context_.begin();  // Check cid
  for (; iter != pdp_context_.end(); ++iter) {
    if (cid == iter->cid && iter->state == PDPContext::ACTIVE) {
      break;
    }
  }

  if (iter == pdp_context_.end()) {
    responses.push_back(kCmeErrorInvalidIndex);  // number
  } else {
    std::stringstream ss;
    ss << "+CGCONTRDP: "
       << iter->cid << ",5,"
       << iter->apn << ","
       << iter->addresses << ","
       << iter->gateways << ","
       << iter->dnses;
    responses.push_back(ss.str());
    responses.push_back("OK");
  }

  client.SendCommandResponse(responses);
}

}  // namespace cuttlefish
