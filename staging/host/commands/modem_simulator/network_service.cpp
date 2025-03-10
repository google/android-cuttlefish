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

#include "host/commands/modem_simulator/network_service.h"

#include <android-base/logging.h>

#include <map>
#include <sstream>

#include "common/libs/utils/files.h"
#include "host/commands/modem_simulator/device_config.h"
#include "host/commands/modem_simulator/nvram_config.h"
#include "host/commands/modem_simulator/thread_looper.h"
namespace cuttlefish {

// string type; two byte location area code in hexadecimal format
static const std::string kAreaCode = "2142";
// string type; four byte GERAN/UTRAN cell ID in hexadecimal format
static const std::string kCellId = "0000B804";

NetworkService::NetworkService(int32_t service_id,
                               ChannelMonitor* channel_monitor,
                               ThreadLooper* thread_looper)
    : ModemService(service_id, this->InitializeCommandHandlers(),
                   channel_monitor, thread_looper),
      keep_signal_strength_changing_loop_(*this) {
  InitializeServiceState();
}

std::vector<CommandHandler> NetworkService::InitializeCommandHandlers() {
  std::vector<CommandHandler> command_handlers = {
      CommandHandler(
          "+CFUN?",
          [this](const Client& client) { this->HandleRadioPowerReq(client); }),
      CommandHandler("+CFUN=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleRadioPower(client, cmd);
                     }),
      CommandHandler("+REMOTECFUN=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleRadioPower(client, cmd);
                     }),
      CommandHandler(
          "+CSQ",
          [this](const Client& client) { this->HandleSignalStrength(client); }),
      CommandHandler("+COPS?",
                     [this](const Client& client) {
                       this->HandleQueryNetworkSelectionMode(client);
                     }),
      CommandHandler("+COPS=3,0;+COPS?;+COPS=3,1;+COPS?;+COPS=3,2;+COPS?",
                     [this](const Client& client) {
                       this->HandleRequestOperator(client);
                     }),
      CommandHandler("+COPS=?",
                     [this](const Client& client) {
                       this->HandleQueryAvailableNetwork(client);
                     }),
      CommandHandler("+COPS=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleSetNetworkSelectionMode(client, cmd);
                     }),
      CommandHandler("+CREG",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleVoiceNetworkRegistration(client, cmd);
                     }),
      CommandHandler("+CGREG",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleDataNetworkRegistration(client, cmd);
                     }),
      CommandHandler("+CEREG",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleDataNetworkRegistration(client, cmd);
                     }),
      CommandHandler("+CTEC?",
                     [this](const Client& client) {
                       this->HandleGetPreferredNetworkType(client);
                     }),
      CommandHandler("+CTEC=?",
                     [this](const Client& client) {
                       this->HandleQuerySupportedTechs(client);
                     }),
      CommandHandler("+CTEC=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleSetPreferredNetworkType(client, cmd);
                     }),
      CommandHandler("+REMOTECTEC",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleReceiveRemoteCTEC(client, cmd);
                     }),
      CommandHandler("+REMOTESIGNAL",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleReceiveRemoteSignal(client, cmd);
                     }),
      CommandHandler("+REMOTEREG",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleReceiveRemoteVoiceDataReg(client, cmd);
                     }),
  };
  return (command_handlers);
}

void NetworkService::InitializeServiceState() {
  radio_state_ = RadioState::RADIO_STATE_OFF;

  modem_radio_capability_ =
      M_MODEM_TECH_GSM | M_MODEM_TECH_WCDMA | M_MODEM_TECH_LTE | M_MODEM_TECH_NR;

  auto nvram_config = NvramConfig::Get();
  auto instance = nvram_config->ForInstance(service_id_);

  // Default to be ""
  current_operator_numeric_ = instance.operator_numeric();
  // Default to be OPER_SELECTION_AUTOMATIC
  oper_selection_mode_ = (OperatorSelectionMode)instance.network_selection_mode();
  // Default to be M_MODEM_TECH_LTE | M_MODEM_TECH_WCDMA | M_MODEM_TECH_GSM;
  preferred_network_mode_ = instance.preferred_network_mode();
  // Default to be M_MODEM_TECH_LTE
  current_network_mode_ = (ModemTechnology)instance.modem_technoloy();

  InitializeNetworkOperator();

  first_signal_strength_request_ = true;
  android_last_signal_time_ = 0;

  keep_signal_strength_changing_loop_.Start();
}

void NetworkService::InitializeNetworkOperator() {
  operator_list_.push_back(
      {"311740", "Android Virtual Operator", "Android", NetworkOperator::OPER_STATE_AVAILABLE});
  operator_list_.push_back(
      {"310300", "Alternative Operator", "Alternative", NetworkOperator::OPER_STATE_AVAILABLE});
  operator_list_.push_back(
      {"310400", "Hermetic Network Operator", "Hermetic", NetworkOperator::OPER_STATE_FORBIDDEN});

  if (oper_selection_mode_ == OperatorSelectionMode::OPER_SELECTION_AUTOMATIC) {
    current_operator_numeric_ = operator_list_.begin()->numeric;
    operator_list_.begin()->operator_state = NetworkOperator::OPER_STATE_CURRENT;
  } else if (oper_selection_mode_ == OperatorSelectionMode::OPER_SELECTION_MANUAL_AUTOMATIC) {
    auto iter = operator_list_.begin();
    for (; iter != operator_list_.end(); ++iter) {
      if (iter->numeric == current_operator_numeric_) {
        break;
      }
    }
    if (iter == operator_list_.end()) {
      current_operator_numeric_ = operator_list_.begin()->numeric;
      operator_list_.begin()->operator_state = NetworkOperator::OPER_STATE_CURRENT;
    } else {
      iter->operator_state = NetworkOperator::OPER_STATE_CURRENT;
    }
  }
}

void NetworkService::InitializeSimOperator() {
  if (sim_service_ == nullptr) {
    return;
  }
  auto sim_operator_numeric = sim_service_->GetSimOperator();
  if (sim_operator_numeric == "") {
    return;
  }

  // ensure the first element is sim_operator_numeric
  for (auto iter = operator_list_.begin(); iter != operator_list_.end();
       ++iter) {
    if (iter->numeric == sim_operator_numeric) {
      std::swap(*iter, *(operator_list_.begin()));
      return;
    }
  }

  {
    const char *operator_numeric_xml = "etc/modem_simulator/files/numeric_operator.xml";
    auto file = cuttlefish::modem::DeviceConfig::DefaultHostArtifactsPath(
        operator_numeric_xml);
    if (!cuttlefish::FileExists(file) || !cuttlefish::FileHasContent(file)) {
      return;
    }

    XMLDocument doc;
    auto err = doc.LoadFile(file.c_str());
    if (err != tinyxml2::XML_SUCCESS) {
      LOG(ERROR) << "unable to load XML file '" << file << " ', error " << err;
      return;
    }
    XMLElement *resources = doc.RootElement();
    if (resources == NULL)  return;

    XMLElement *stringArray = resources->FirstChildElement("string-array");
    if (stringArray == NULL) return;

    XMLElement *item = stringArray->FirstChildElement("item");
    while (item) {
      const XMLAttribute *attr_numeric = item->FindAttribute("numeric");
      std::string numeric = attr_numeric ? attr_numeric->Value() : "";
      if (numeric == sim_operator_numeric) {
        break;
      }
      item = item->NextSiblingElement("item");
    }
    if (item) {
      std::string names = item->GetText();
      auto pos = names.find('=');
      if (pos != std::string::npos) {
        auto long_name = names.substr(0, pos);
        auto short_name = names.substr(pos + 1);
        NetworkOperator sim_operator(sim_operator_numeric, long_name,
            short_name, NetworkOperator::OPER_STATE_AVAILABLE);
        operator_list_.insert(operator_list_.begin(), sim_operator);
      }
    }
  }
  InitializeNetworkOperator();
}

void NetworkService::SetupDependency(MiscService* misc, SimService* sim,
                                     DataService* data) {
  misc_service_ = misc;
  sim_service_ = sim;
  data_service_ = data;
  InitializeSimOperator();
}

void NetworkService::OnSimStatusChanged(SimService::SimStatus sim_status) {
  if (radio_state_ == RadioState::RADIO_STATE_OFF) {
    return;  // RegistrationState::NET_REGISTRATION_UNREGISTERED unchanged
  }
  if (sim_status == SimService::SIM_STATUS_READY) {
    voice_registration_status_.registration_state = NET_REGISTRATION_HOME;
  } else {
    voice_registration_status_.registration_state = NET_REGISTRATION_EMERGENCY;
    // 3GPP TS 24.008 [8] and 3GPP TS 24.301 [83] specify the condition
    // when the MT is considered as attached for emergency bearer services.
    // applicable only when <AcT> indicates 2,4,5,6
    // Note: not saved to nvram config due to sim status may change after reboot
    current_network_mode_ = M_MODEM_TECH_WCDMA;
  }
  thread_looper_->Post(
      makeSafeCallback(this, &NetworkService::UpdateRegisterState,
                       voice_registration_status_.registration_state),
      std::chrono::seconds(1));
}

/**
 * AT+CFUN
 *   Set command selects the level of functionality <fun> in the MT. Level
 * "full functionality" is where the highest level of power is drawn.
 * "Minimum functionality" is where minimum power is drawn. Level of functionality
 * between these may also be specified by manufacturers. When supported by
 * manufacturers, MT resetting with <rst> parameter may be utilized
 *
 * Command                Possible response(s)
 * +CFUN=[<fun>[,<rst>]]    +CME ERROR: <err>
 * +CFUN?                   +CFUN: <fun>
 *                          +CME ERROR: <err>
 *
 * <fun>: integer type
 *   0 minimum functionality
 *   1 full functionality. Enable (turn on) the transmit and receive RF circuits
 *     for all supported radio access technologies.
 *   2 disable (turn off) MT transmit RF circuits only
 *   3 disable (turn off) MT receive RF circuits only
 *   4 disable (turn off) both MT transmit and receive RF circuits
 *   5...127 reserved for manufacturers as intermediate states between full
 *           and minimum functionality
 *   128 Full functionality with radio access support according to the setting of +CSRA.
 *   129 Prepare for shutdown.
 *
 * see RIL_REQUEST_RADIO_POWER in RIL
 */
void NetworkService::HandleRadioPowerReq(const Client& client) {
  std::stringstream ss;
  ss << "+CFUN: " << radio_state_;

  std::vector<std::string> responses;
  responses.push_back(ss.str());
  responses.push_back("OK");

  client.SendCommandResponse(responses);
}

void NetworkService::HandleRadioPower(const Client& client, std::string& command) {
  CommandParser cmd(command);
  cmd.SkipPrefix();
  int on = cmd.GetNextInt();
  switch (on) {
    case 0:
      radio_state_ = RadioState::RADIO_STATE_OFF;
      UpdateRegisterState(NET_REGISTRATION_UNREGISTERED);
      break;
    case 1:
      radio_state_ = RadioState::RADIO_STATE_ON;
      if (sim_service_ != nullptr) {
        auto sim_status = sim_service_->GetSimStatus();
        OnSimStatusChanged(sim_status);
      }
      break;
    default:
      client.SendCommandResponse(kCmeErrorOperationNotSupported);
      return;
  }

  client.SendCommandResponse("OK");
}

bool NetworkService::WakeupFromSleep() {
  // It has not called once yet
  if (android_last_signal_time_ == 0) {
      return false;
  }
  // Heuristics: if guest has not asked for signal strength
  // for 2 minutes, we assume it is caused by host sleep
  time_t now = time(0);
  const bool wakeup_from_sleep = (now > android_last_signal_time_ + 120);
  return wakeup_from_sleep;
}

/**
 * IMPORTANT NOTE: Current implementation of AT+CSQ differs from standards
 * described in TS 27.007 8.5 which only only supports RSSI and BER.
 *
 * TODO(b/206814247): Rename AT+CSQ command.
 *
 * AT+CSQ
 *   Execution command returns received signal strength indication. This is a
 *   Cuttlefish specific command.
 *
 * Command            Possible response(s)
 * AT+CSQ             +CSQ: <gsm_rssi>,<gsm_ber>,<cdma_dbm>,
 *                      <cdma_ecio>,<evdo_dbm>,<evdo_ecio>,<evdo_snr>,
 *                      <lte_rssi>,<lte_rsrp>,<lte_rsrq>,<lte_rssnr>,
 *                      <lte_cqi>,<lte_ta>,<tdscdma_rscp>,<wcdma_rssi>,
 *                      <wcdma_ber>,<nr_ss_rsrp>,<nr_ss_rsrq>,<nr_ss_sinr>,
 *                      <nr_csi_rsrp>,<nr_csi_rsrq>,<nr_csi_sinr>
 *                    +CME ERROR: <err>
 *
 * <gsm_rssi>: Valid values are (0-31, 99) as defined in TS 27.007 8.5.
 * <gsm_ber>: Bit error rate (0-7, 99) as defined in TS 27.007 8.5.
 * <cdma_dbm>: Valid values are positive integers.
 *   This value is the actual RSSI value multiplied by -1.
 *   Example: If the actual RSSI is -75, then this response value will be 75.
 * <cdma_ecio>: Valid values are positive integers.
 *   This value is the actual Ec/Io multiplied by -10.
 *   Example: If the actual Ec/Io is -12.5 dB, then this response value will
 *   be 125.
 * <evdo_dbm>: Refer cdma_dbm.
 * <evdo_ecio>: Refer cdma_ecio.
 * <evdo_snr>: Valid values are 0-8.
 *   8 is the highest signal to noise ratio.
 * <lte_rssi>: Refer gsm_rssi.
 * <lte_rsrp>:
 *   The current Reference Signal Receive Power in dBm multiplied by -1.
 *   Range: 44 to 140 dBm.
 *   INT_MAX: 0x7FFFFFFF denotes invalid value.
 *   Reference: 3GPP TS 36.133 9.1.4.
 * <lte_rsrq>:
 *   The current Reference Signal Receive Quality in dB multiplied by -1.
 *   Range: 20 to 3 dB.
 *   INT_MAX: 0x7FFFFFFF denotes invalid value.
 *   Reference: 3GPP TS 36.133 9.1.7.
 * <lte_rssnr>:
 *   The current reference signal signal-to-noise ratio in 0.1 dB units.
 *   Range: -200 to +300 (-200 = -20.0 dB, +300 = 30dB).
 *   INT_MAX : 0x7FFFFFFF denotes invalid value.
 *   Reference: 3GPP TS 36.101 8.1.1.
 * <lte_cqi>: The current Channel Quality Indicator.
 *   Range: 0 to 15.
 *   INT_MAX : 0x7FFFFFFF denotes invalid value.
 *   Reference: 3GPP TS 36.101 9.2, 9.3, A.4.
 * <lte_ta>:
 *   Timing advance in micro seconds for a one way trip from cell to device.
 *   Approximate distance can be calculated using 300m/us * timingAdvance.
 *   Range: 0 to 0x7FFFFFFE.
 *   INT_MAX : 0x7FFFFFFF denotes invalid value.
 *   Reference: 3GPP 36.321 section 6.1.3.5.
 * <tdscdma_rscp>: P-CCPCH RSCP as defined in TS 25.225 5.1.1.
 *   Valid values are (0-96, 255) as defined in TS 27.007 8.69.
 *   INT_MAX denotes that the value is invalid/unreported.
 * <wcdma_rssi>: Refer gsm_rssi.
 * <wcdma_ber>: Refer gsm_ber.
 * <nr_ss_rsrp>: SS reference signal received power, multiplied by -1.
 *   Reference: 3GPP TS 38.215.
 *   Range [44, 140], INT_MAX means invalid/unreported.
 * <nr_ss_rsrq>: SS reference signal received quality, multiplied by -1.
 *   Reference: 3GPP TS 38.215.
 *   Range [3, 20], INT_MAX means invalid/unreported.
 * <nr_ss_sinr>: SS signal-to-noise and interference ratio.
 *   Reference: 3GPP TS 38.215 section 5.1.*, 3GPP TS 38.133 section 10.1.16.1.
 *   Range [-23, 40], INT_MAX means invalid/unreported.
 * <nr_csi_rsrp>: CSI reference signal received power, multiplied by -1.
 *   Reference: 3GPP TS 38.215.
 *   Range [44, 140], INT_MAX means invalid/unreported.
 * <nr_csi_rsrq>: CSI reference signal received quality, multiplied by -1.
 *   Reference: 3GPP TS 38.215.
 *   Range [3, 20], INT_MAX means invalid/unreported.
 * <nr_csi_sinr>: CSI signal-to-noise and interference ratio.
 *   Reference: 3GPP TS 138.215 section 5.1.*, 3GPP TS 38.133 section 10.1.16.1.
 *   Range [-23, 40], INT_MAX means invalid/unreported.
 *
 * see RIL_REQUEST_SIGNAL_STRENGTH in RIL
 */
void NetworkService::HandleSignalStrength(const Client& client) {
  std::vector<std::string> responses;
  std::stringstream ss;

  if (WakeupFromSleep()) {
    misc_service_->TimeUpdate();
  } else if (first_signal_strength_request_) {
    first_signal_strength_request_ = false;
    misc_service_->TimeUpdate();
  }

  android_last_signal_time_ = time(0);

  auto response = BuildCSQCommandResponse(GetCurrentSignalStrength());

  responses.push_back(response);
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

bool NetworkService::IsHasNetwork() {
  return radio_state_ != RADIO_STATE_OFF &&
         oper_selection_mode_ !=
             OperatorSelectionMode::OPER_SELECTION_DEREGISTRATION;
}

/**
 * AT+COPS
 *    Set command forces an attempt to select and register to the
 *    GSM/UMTS/EPS/5GS network operator using the SIM/USIM card installed
 *    in the currently selected card slot.
 *
 * command                         Possible response(s)
 * +COPS=[<mode>[,<format>          +CME ERROR: <err>
 *       [,<oper>[,<AcT>]]]]
 *
 * +COPS?                           +COPS: <mode>[,<format>,<oper>[,<AcT>]]
 *                                  +CME ERROR: <err>
 *
 * +COPS=?                          +COPS: [list of supported (<stat>,
 *                                         long alphanumeric <oper>,
 *                                         short alphanumeric <oper>,
 *                                         numeric <oper>[,<AcT>])s]
 *                                      [,,(list of supported <mode>s),
 *                                      (list of supported <format>s)]
 *                                  +CME ERROR: <err>
 *
 * <mode>: integer type
 *       0 automatic (<oper> field is ignored)
 *       1 manual (<oper> field shall be present, and <AcT> optionally)
 *       2 deregister from network
 *       3 set only <format> (for read command +COPS?), do not attempt
 *       registration/deregistration (<oper> and <AcT> fields are ignored);
 *        this value is not applicable in read command response
 *       4 manual/automatic (<oper> field shall be present); if manual selection fails, automatic mode (<mode>=0) is entered
 * <format>: integer type
 *         0 long format alphanumeric <oper>
 *         1 short format alphanumeric <oper>
 *         2 numeric <oper>
 * <oper>: string type;
 * <format> indicates if the format is alphanumeric or numeric;
 * <stat>: integer type
 *       0 unknown
 *       1 available
 *       2 current
 *       3 forbidden
 * <AcT>: integer type; access technology selected
 *      0 GSM
 *      1 GSM Compact
 *      2 UTRAN
 *      3 GSM w/EGPRS (see NOTE 1)
 *      4 UTRAN w/HSDPA (see NOTE 2)
 *      5 UTRAN w/HSUPA (see NOTE 2)
 *      6 UTRAN w/HSDPA and HSUPA (see NOTE 2)
 *      7 E-UTRAN
 *      8 EC-GSM-IoT (A/Gb mode) (see NOTE 3)
 *      9 E-UTRAN (NB-S1 mode) (see NOTE 4)
 *      10 E-UTRA connected to a 5GCN (see NOTE 5)
 *      11 NR connected to a 5GCN (see NOTE 5)
 *      12 NG-RAN
 *      13 E-UTRA-NR dual connectivity (see NOTE 6)
 *
 *  see RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC or
 *      RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE or
 *      RIL_REQUEST_OPERATOR in RIL
 */
void NetworkService::HandleQueryNetworkSelectionMode(const Client& client) {
  std::vector<std::string> responses;
  std::stringstream ss;

  if (!IsHasNetwork()) {
    ss << "+COPS: 0,0,0";
  } else {
    auto iter = operator_list_.begin();
    for (; iter != operator_list_.end(); ++iter) {
      if (iter->numeric == current_operator_numeric_) {
        break;
      }
    }
    if (iter != operator_list_.end()) {
      ss << "+COPS: " << oper_selection_mode_ << ",2," << iter->numeric;
    } else {
      ss << "+COPS: " << oper_selection_mode_ << ",0,0";
    }
  }
  responses.push_back(ss.str());
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/* AT+COPS=3,0;+COPS?;+COPS=3,1;+COPS?;+COPS=3,2;+COPS? */
void NetworkService::HandleRequestOperator(const Client& client) {
  if (!IsHasNetwork()) {
    client.SendCommandResponse(kCmeErrorOperationNotAllowed);
    return;
  }

  auto iter = operator_list_.begin();
  for (; iter != operator_list_.end(); ++iter) {
    if (iter->numeric == current_operator_numeric_) {
      break;
    }
  }
  if (iter == operator_list_.end()) {
    client.SendCommandResponse(kCmeErrorNoNetworkService);
    return;
  }

  std::vector<std::string> responses;
  std::vector<std::stringstream> ss;
  ss.resize(3);

  ss[0] << "+COPS: 0,0," << iter->long_name;
  ss[1] << "+COPS: 0,1," << iter->short_name;
  ss[2] << "+COPS: 0,2," << iter->numeric;

  responses.push_back(ss[0].str());
  responses.push_back(ss[1].str());
  responses.push_back(ss[2].str());
  responses.push_back("OK");

  client.SendCommandResponse(responses);
}

/* AT+COPS=? */
void NetworkService::HandleQueryAvailableNetwork(const Client& client) {
  std::vector<std::string> responses;
  std::stringstream ss;

  for (auto iter = operator_list_.begin(); iter != operator_list_.end(); ++iter) {
    ss.clear();
    ss << "+COPS: (" << iter->operator_state << ","
                     << iter->long_name << ","
                     << iter->short_name << ","
                     << iter->numeric << "),";
    responses.push_back(ss.str());
    ss.str("");
  }

  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/* AT+COPS=mode,format,operatorNumeric,act */
void NetworkService::HandleSetNetworkSelectionMode(const Client& client, std::string& command) {
  std::vector<std::string> responses;
  std::stringstream ss;

  CommandParser cmd(command);
  cmd.SkipPrefix();

  int mode = (OperatorSelectionMode)cmd.GetNextInt();
  cmd.SkipPrefix();  // Default to be numeric

  auto& registration_state = voice_registration_status_.registration_state;

  switch (mode) {
    // <oper> field is ignored
    case OperatorSelectionMode::OPER_SELECTION_AUTOMATIC: {
      oper_selection_mode_ = OperatorSelectionMode::OPER_SELECTION_AUTOMATIC;

      // The first operator stored in operator_list_ map default to be
      // the automatic selected operator
      auto iter = operator_list_.begin();
      current_operator_numeric_ = iter->numeric;
      iter->operator_state = NetworkOperator::OPER_STATE_CURRENT;

      // Change operator state stored in the operator_list_ map
      ++iter;
      for (; iter != operator_list_.end(); ++iter) {
        if (iter->operator_state == NetworkOperator::OPER_STATE_CURRENT) {
          iter->operator_state = NetworkOperator::OPER_STATE_AVAILABLE;
          break;
        }
      }

      registration_state = NET_REGISTRATION_HOME;
      client.SendCommandResponse("OK");
      break;
    }

    // <oper> field shall be present, and <AcT> optionally
    case OperatorSelectionMode::OPER_SELECTION_MANUAL: {
      oper_selection_mode_ = OperatorSelectionMode::OPER_SELECTION_MANUAL;
      current_operator_numeric_ = cmd.GetNextStr();
      auto iter = operator_list_.begin();
      for (; iter != operator_list_.end(); ++iter) {
        if (iter->numeric == current_operator_numeric_) {
          break;
        }
      }
      // If the selected operator is not available, no other operator shall be
      // selected (except <mode>=4).
      if (iter == operator_list_.end()) {
        registration_state = NET_REGISTRATION_UNKNOWN;
        client.SendCommandResponse(kCmeErrorNoNetworkService);
        break;
      }

      // Change operator state stored in the operator_list_ vector
      iter->operator_state = NetworkOperator::OPER_STATE_CURRENT;
      iter = operator_list_.begin();
      for (; iter != operator_list_.end(); ++iter) {
        if (iter->numeric != current_operator_numeric_ &&
            iter->operator_state == NetworkOperator::OPER_STATE_CURRENT) {
          iter->operator_state = NetworkOperator::OPER_STATE_AVAILABLE;
        }
      }

      // If the selected access technology is not available, then the same operator
      // shall be selected in other access technology
      int act = cmd.GetNextInt();
      if (act != -1) {
        auto tech = getTechFromNetworkType((NetworkRegistrationStatus::AccessTechnoloy)act);
        if (tech & modem_radio_capability_) {
          current_network_mode_ = tech;
        }  // else: remain current network mode unchanged
      }  // else: remain act unchanged

      if (iter->operator_state == NetworkOperator::OPER_STATE_FORBIDDEN) {
        registration_state = NET_REGISTRATION_DENIED;
      } else if (iter->operator_state == NetworkOperator::OPER_STATE_UNKNOWN) {
        registration_state = NET_REGISTRATION_UNKNOWN;
      } else {
        registration_state = NET_REGISTRATION_HOME;
      }
      client.SendCommandResponse("OK");
      break;
    }

    case OperatorSelectionMode::OPER_SELECTION_DEREGISTRATION: {
      oper_selection_mode_ = OperatorSelectionMode::OPER_SELECTION_DEREGISTRATION;
      registration_state = NET_REGISTRATION_UNREGISTERED;
      client.SendCommandResponse("OK");
      break;
    }

    // <oper> field shall be present
    case OperatorSelectionMode::OPER_SELECTION_MANUAL_AUTOMATIC: {
      oper_selection_mode_ = OperatorSelectionMode::OPER_SELECTION_MANUAL_AUTOMATIC;
      auto operator_numeric = cmd.GetNextStr();
      // If manual selection fails, automatic mode (<mode>=0) is entered
      auto iter = operator_list_.begin();
      for (; iter != operator_list_.end(); ++iter) {
        if (iter->numeric == operator_numeric) {
          break;
        }
      }
      // If the selected operator is not available, no other operator shall be
      // selected (except <mode>=4)
      if (iter != operator_list_.end() ||
          iter->operator_state == NetworkOperator::OPER_STATE_AVAILABLE) {
        current_operator_numeric_ = iter->numeric;
      }

      // Change operator state stored in the operator_list_ vector
      iter = operator_list_.begin();
      for (; iter != operator_list_.end(); ++iter) {
        if (iter->numeric == current_operator_numeric_) {
          iter->operator_state = NetworkOperator::OPER_STATE_CURRENT;
        } else if (iter->operator_state == NetworkOperator::OPER_STATE_CURRENT) {
          iter->operator_state = NetworkOperator::OPER_STATE_AVAILABLE;
        }
      }

      registration_state = NET_REGISTRATION_HOME;
      client.SendCommandResponse("OK");
      break;
    }

    default:
      client.SendCommandResponse(kCmeErrorInCorrectParameters);
      return;
  }

  // Save the value anyway, no matter the value changes or not
  auto nvram_config = NvramConfig::Get();
  auto instance = nvram_config->ForInstance(service_id_);
  instance.set_network_selection_mode(oper_selection_mode_);
  instance.set_operator_numeric(current_operator_numeric_);

  NvramConfig::SaveToFile();

  thread_looper_->Post(
      makeSafeCallback(this, &NetworkService::UpdateRegisterState,
                       registration_state),
      std::chrono::seconds(1));
}

NetworkService::NetworkRegistrationStatus::AccessTechnoloy
NetworkService::getNetworkTypeFromTech(ModemTechnology modemTech) {
  switch (modemTech) {
   case ModemTechnology::M_MODEM_TECH_GSM:
     return NetworkRegistrationStatus::ACESS_TECH_EGPRS;
   case ModemTechnology::M_MODEM_TECH_WCDMA:
     return NetworkRegistrationStatus::ACESS_TECH_HSPA;
   case ModemTechnology::M_MODEM_TECH_LTE:
     return NetworkRegistrationStatus::ACESS_TECH_EUTRAN;
   case ModemTechnology::M_MODEM_TECH_NR:
     return NetworkRegistrationStatus::ACESS_TECH_NR;
   default:
     return NetworkRegistrationStatus::ACESS_TECH_EGPRS;
  }
}

NetworkService::ModemTechnology NetworkService::getTechFromNetworkType(
    NetworkRegistrationStatus::AccessTechnoloy act) {
  switch (act) {
    case NetworkRegistrationStatus::ACESS_TECH_GSM:
    case NetworkRegistrationStatus::ACESS_TECH_GSM_COMPACT:
    case NetworkRegistrationStatus::ACESS_TECH_EGPRS:
    case NetworkRegistrationStatus::ACESS_TECH_EC_GSM_IoT:
      return ModemTechnology::M_MODEM_TECH_GSM;

    case NetworkRegistrationStatus::ACESS_TECH_UTRAN:
    case NetworkRegistrationStatus::ACESS_TECH_HSDPA:
    case NetworkRegistrationStatus::ACESS_TECH_HSUPA:
    case NetworkRegistrationStatus::ACESS_TECH_HSPA:
      return ModemTechnology::M_MODEM_TECH_WCDMA;

    case NetworkRegistrationStatus::ACESS_TECH_EUTRAN:
    case NetworkRegistrationStatus::ACESS_TECH_E_UTRAN:
    case NetworkRegistrationStatus::ACESS_TECH_E_UTRA:
      return ModemTechnology::M_MODEM_TECH_LTE;

    case NetworkRegistrationStatus::ACESS_TECH_NR:
    case NetworkRegistrationStatus::ACESS_TECH_NG_RAN:
    case NetworkRegistrationStatus::ACESS_TECH_E_UTRA_NR:
      return ModemTechnology::M_MODEM_TECH_NR;

    default:
      return ModemTechnology::M_MODEM_TECH_GSM;
  }
}

/**
 * AT+CREG
 *   Set command controls the presentation of an unsolicited result code
 * +CREG: <stat> when <n>=1 and there is a change in the MT’s circuit
 * mode network registration status in GERAN/UTRAN/E-UTRAN, or unsolicited
 * result code +CREG: <stat>[,[<lac>],[<ci>],[<AcT>]]
 * when <n>=2 and there is a change of the network cell in GERAN/UTRAN/E-UTRAN. The
 * parameters <AcT>, <lac> and <ci> are sent only if available.
 * The value <n>=3 further extends the unsolicited result code with [,<cause_type>,
 * <reject_cause>], when available, when the value of <stat> changes.
 *
 * command             Possible response(s)
 * +CREG=[<n>]         +CME ERROR: <err>
 *
 * +CREG?             +CREG: <n>,<stat>[,[<lac>],[<ci>],[<AcT>]
 *                            [,<cause_type>,<reject_cause>]]
 *
 * <n>: integer type
 *    0 disable network registration unsolicited result code
 *    1 enable network registration unsolicited result code +CREG: <stat>
 *    2 enable network registration and location information unsolicited
 *      result code +CREG: <stat>[,[<lac>],[<ci>],[<AcT>]]
 *    3 enable network registration, location information and cause value
 *      information unsolicited result code +CREG: <stat>[,[<lac>],[<ci>],
 *      [<AcT>][,<cause_type>,<reject_cause>]]
 *
 * <stat>: integer type;
 *      0 not registered, MT is not currently searching a new operator to register to
 *      1 registered, home network
 *      2 not registered, but MT is currently searching a new operator to register to
 *      3 registration denied
 *      4 unknown (e.g. out of GERAN/UTRAN/E-UTRAN coverage)
 *      5 registered, roaming
 *
 * <lac>: string type; two byte location area code (when <AcT> indicates
 *        value 0 to 6), or tracking area code (when <AcT> indicates
 *        value 7). In hexadecimal format
 * <ci>: string type; four byte GERAN/UTRAN/E-UTRAN cell ID in
 *       hexadecimal format
 * <AcT>: refer line 190
 *
 * see RIL_REQUEST_VOICE_REGISTRATION_STATE or in RIL
*/
void NetworkService::HandleVoiceNetworkRegistration(const Client& client,
                                                    std::string& command) {
  std::vector<std::string> responses;
  std::stringstream ss;

  CommandParser cmd(command);
  cmd.SkipPrefix();
  if (*cmd == "AT+CREG?") {
    ss << "+CREG: " << voice_registration_status_.unsol_mode << ","
                    << voice_registration_status_.registration_state;
    if (voice_registration_status_.unsol_mode ==
            NetworkRegistrationStatus::REGISTRATION_UNSOL_ENABLED_FULL &&
       (voice_registration_status_.registration_state ==
            NET_REGISTRATION_HOME ||
        voice_registration_status_.registration_state ==
            NET_REGISTRATION_ROAMING ||
        voice_registration_status_.registration_state ==
            NET_REGISTRATION_EMERGENCY)) {
      ss << ",\"" << kAreaCode << "\"" << ",\"" << kCellId << "\","
                  << voice_registration_status_.network_type;
    }

    responses.push_back(ss.str());
  } else {
    int n = cmd.GetNextInt();
    switch (n) {
      case 0:
        voice_registration_status_.unsol_mode =
            NetworkRegistrationStatus::REGISTRATION_UNSOL_DISABLED;
        break;
      case 1:
        voice_registration_status_.unsol_mode =
            NetworkRegistrationStatus::REGISTRATION_UNSOL_ENABLED;
        break;
      case 2:
        voice_registration_status_.unsol_mode =
            NetworkRegistrationStatus::REGISTRATION_UNSOL_ENABLED_FULL;
        break;
      default:
        client.SendCommandResponse(kCmeErrorInCorrectParameters);
        return;
    }
  }
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/**
 * AT+CGREG
 * The set command controls the presentation of an unsolicited result
 *  code +CGREG: <stat> when <n>=1 and there is a change in the MT's
 *  GPRS network registration status, or code +CGREG: <stat>[,<lac>,
 *  <ci>[,<AcT>]] when <n>=2 and there is a change of the network cell.
 *
 * command             Possible response(s)
 * +CGREG=[<n>]         +CME ERROR: <err>
 *
 * +CGREG?             when <n>=0, 1, 2 or 3 and command successful:
 *                     +CGREG: <n>,<stat>[,[<lac>],[<ci>],[<AcT>],
 *                             [<rac>][,<cause_type>,<reject_cause>]]
 *                     when <n>=4 or 5 and command successful:
 *                     +CGREG: <n>,<stat>[,[<lac>],[<ci>],[<AcT>],
 *                             [<rac>][,[<cause_type>],[<reject_cause>][,
 *                             [<Active-Time>],[<Periodic-RAU>],
 *                             [<GPRS-READY-timer>]]]]
 *                             [,<cause_type>,<reject_cause>]]
 *
 * note: see AT+CREG
 *
 * see  RIL_REQUEST_DATA_REGISTRATION_STATE in RIL
 */
void NetworkService::HandleDataNetworkRegistration(const Client& client,
                                                   std::string& command) {
  std::vector<std::string> responses;
  std::stringstream ss;
  std::string prefix;

  CommandParser cmd(command);
  cmd.SkipPrefix();
  if (command.find("CGREG") != std::string::npos) {
    prefix = "+CGREG: ";
  } else if (command.find("CEREG") != std::string::npos){
    prefix = "+CEREG: ";
  }

  if (*cmd == "AT+CGREG?" || *cmd == "AT+CEREG?") {
    ss << prefix << data_registration_status_.unsol_mode << ","
                 << data_registration_status_.registration_state;
    if (voice_registration_status_.unsol_mode ==
            NetworkRegistrationStatus::REGISTRATION_UNSOL_ENABLED_FULL &&
       (voice_registration_status_.registration_state ==
            NET_REGISTRATION_HOME ||
        voice_registration_status_.registration_state ==
            NET_REGISTRATION_ROAMING ||
        voice_registration_status_.registration_state ==
            NET_REGISTRATION_EMERGENCY)) {
      data_registration_status_.network_type =
          getNetworkTypeFromTech(current_network_mode_);
      ss << ",\"" << kAreaCode << "\"" << ",\"" << kCellId << "\"" << ","
                  << data_registration_status_.network_type;
    }
    responses.push_back(ss.str());
  } else {
    int n = cmd.GetNextInt();
    switch (n) {
      case 0:
        data_registration_status_.unsol_mode =
            NetworkRegistrationStatus::REGISTRATION_UNSOL_DISABLED;
        break;
      case 1:
        data_registration_status_.unsol_mode =
            NetworkRegistrationStatus::REGISTRATION_UNSOL_ENABLED;
        break;
      case 2:
        data_registration_status_.unsol_mode =
            NetworkRegistrationStatus::REGISTRATION_UNSOL_ENABLED_FULL;
        break;
      default:
        client.SendCommandResponse(kCmeErrorInCorrectParameters);
        return;
    }
  }
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/* AT+CTEC? */
void NetworkService::HandleGetPreferredNetworkType(const Client& client) {
  std::vector<std::string> responses;
  std::stringstream ss;

  ss << "+CTEC: " << current_network_mode_ << "," << std::hex << preferred_network_mode_;

  responses.push_back(ss.str());
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/* AT+CTEC=? */
void NetworkService::HandleQuerySupportedTechs(const Client& client) {
  std::vector<std::string> responses;
  std::stringstream ss;
  ss << "+CTEC: 0,1,5,6";  // NR | LTE | WCDMA | GSM
  responses.push_back(ss.str());
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/**
 * Preferred mode bitmask. This is actually 4 byte-sized bitmasks with different priority values,
 * in which the byte number from LSB to MSB give the priority.
 *
 *          |MSB|   |   |LSB
 * value:   |00 |00 |00 |00
 * byte #:  |3  |2  |1  |0
 *
 * Higher byte order give higher priority. Thus, a value of 0x0000000f represents
 * a preferred mode of GSM, WCDMA, CDMA, and EvDo in which all are equally preferrable, whereas
 * 0x00000201 represents a mode with GSM and WCDMA, in which WCDMA is preferred over GSM
 */
int NetworkService::getModemTechFromPrefer(int preferred_mask) {
  int i, j;

  // Current implementation will only return the highest priority,
  // lowest numbered technology that is set in the mask.
  for (i = 3 ; i >= 0; i--) {
    for (j = 7 ; j >= 0 ; j--) {
      if (preferred_mask & (1 << (j + 8 * i)))
          return 1 << j;
    }
  }
  // This should never happen. Just to please the compiler.
  return ModemTechnology::M_MODEM_TECH_GSM;
}

void NetworkService::UpdateRegisterState(RegistrationState state ) {
  voice_registration_status_.registration_state = state;
  data_registration_status_.registration_state = state;
  voice_registration_status_.network_type =
      (NetworkRegistrationStatus::AccessTechnoloy)getNetworkTypeFromTech(current_network_mode_);
  data_registration_status_.network_type =
      (NetworkRegistrationStatus::AccessTechnoloy)getNetworkTypeFromTech(current_network_mode_);

  OnVoiceRegisterStateChanged();
  OnDataRegisterStateChanged();
  OnSignalStrengthChanged();

  int cellBandwidthDownlink = 5000;
  const int UNKNOWN = 0;
  const int MMWAVE = 4;
  int freq = UNKNOWN;
  if (current_network_mode_ == M_MODEM_TECH_NR) {
    freq = MMWAVE;
    cellBandwidthDownlink = 50000;
  }

  data_service_->onUpdatePhysicalChannelconfigs(current_network_mode_, freq,
                                                cellBandwidthDownlink);
}

/* AT+CTEC=current,preferred */
void NetworkService::HandleSetPreferredNetworkType(const Client& client, std::string& command) {
  std::vector<std::string> responses;
  std::stringstream ss;
  int preferred_mask_new;
  CommandParser cmd(command);
  cmd.SkipPrefix();

  int current = cmd.GetNextInt();
  std::string preferred(cmd.GetNextStr());
  preferred_mask_new = std::stoi(preferred, nullptr, 16);
  if (preferred_mask_new != preferred_network_mode_) {
    current_network_mode_ = (ModemTechnology)getModemTechFromPrefer(preferred_mask_new);
    preferred_network_mode_ = preferred_mask_new;
  }

  if (current != current_network_mode_) {
    UpdateRegisterState(NET_REGISTRATION_UNREGISTERED);

    ss << "+CTEC: "<< current_network_mode_;

    thread_looper_->Post(
        makeSafeCallback(this, &NetworkService::UpdateRegisterState,
                         NET_REGISTRATION_HOME),
        std::chrono::milliseconds(200));
  } else {
    ss << "+CTEC: DONE";
  }

  auto nvram_config = NvramConfig::Get();
  auto instance = nvram_config->ForInstance(service_id_);
  instance.set_modem_technoloy(current_network_mode_);
  instance.set_preferred_network_mode(preferred_network_mode_);

  NvramConfig::SaveToFile();

  responses.push_back(ss.str());
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

void NetworkService::OnVoiceRegisterStateChanged() {
  std::stringstream ss;

  switch (voice_registration_status_.unsol_mode) {
    case NetworkRegistrationStatus::REGISTRATION_UNSOL_ENABLED:
      ss << "+CREG: " << voice_registration_status_.registration_state;
      break;
    case NetworkRegistrationStatus::REGISTRATION_UNSOL_ENABLED_FULL:
      ss << "+CREG: " << voice_registration_status_.registration_state;
      if (voice_registration_status_.registration_state ==
              NET_REGISTRATION_HOME ||
          voice_registration_status_.registration_state ==
              NET_REGISTRATION_ROAMING) {
        ss << ",\""<< kAreaCode << "\",\"" << kCellId << "\","
                 << voice_registration_status_.network_type;
      }
      break;
    default :
      return;
  }
  SendUnsolicitedCommand(ss.str());
}

void NetworkService::OnDataRegisterStateChanged() {
  std::stringstream ss;

  switch (data_registration_status_.unsol_mode) {
    case NetworkRegistrationStatus::REGISTRATION_UNSOL_ENABLED:
      ss << "+CGREG: " << data_registration_status_.registration_state;
      if (data_registration_status_.network_type ==
              NetworkRegistrationStatus::ACESS_TECH_EUTRAN) {
        ss << "\r+CEREG: " << data_registration_status_.registration_state;
      }
      break;
    case NetworkRegistrationStatus::REGISTRATION_UNSOL_ENABLED_FULL:
      ss << "+CGREG: " << data_registration_status_.registration_state;
      if (data_registration_status_.registration_state ==
                NET_REGISTRATION_HOME ||
          data_registration_status_.registration_state ==
                NET_REGISTRATION_ROAMING) {
        ss << ",\"" << kAreaCode << "\",\"" << kCellId << "\","
           << data_registration_status_.network_type;
      }
      if (data_registration_status_.network_type ==
                NetworkRegistrationStatus::ACESS_TECH_EUTRAN) {
          ss << "\r+CEREG: " << data_registration_status_.registration_state;
          if (data_registration_status_.registration_state ==
                  NET_REGISTRATION_HOME ||
              data_registration_status_.registration_state ==
                  NET_REGISTRATION_ROAMING) {
            ss << ",\"" << kAreaCode << "\",\"" << kCellId << "\","
                      << data_registration_status_.network_type;
          }
      }
      break;
    default:
      return;
  }
  SendUnsolicitedCommand(ss.str());
}

int NetworkService::GetValueInRange(const std::pair<int, int>& range,
                                    int percent) {
  int range_size = range.second - range.first + 1;
  return range.first + (int)((percent / 101.0) * range_size);
}

std::string NetworkService::BuildCSQCommandResponse(
    const SignalStrength& signal_strength) {
  std::stringstream ss;
  // clang-format off
  ss << "+CSQ: "
     << signal_strength.gsm_rssi << ","
     << signal_strength.gsm_ber << ","
     << signal_strength.cdma_dbm << ","
     << signal_strength.cdma_ecio << ","
     << signal_strength.evdo_dbm << ","
     << signal_strength.evdo_ecio << ","
     << signal_strength.evdo_snr << ","
     << signal_strength.lte_rssi << ","
     << signal_strength.lte_rsrp << ","
     << signal_strength.lte_rsrq << ","
     << signal_strength.lte_rssnr << ","
     << signal_strength.lte_cqi << ","
     << signal_strength.lte_ta << ","
     << signal_strength.tdscdma_rscp << ","
     << signal_strength.wcdma_rssi << ","
     << signal_strength.wcdma_ber << ","
     << signal_strength.nr_ss_rsrp << ","
     << signal_strength.nr_ss_rsrq << ","
     << signal_strength.nr_ss_sinr << ","
     << signal_strength.nr_csi_rsrp << ","
     << signal_strength.nr_csi_rsrq << ","
     << signal_strength.nr_csi_sinr;
  // clang-format on
  return ss.str();
}

NetworkService::SignalStrength NetworkService::GetCurrentSignalStrength() {
  NetworkService::SignalStrength result;
  if (!IsHasNetwork()) {
    return result;
  }
  int percent = signal_strength_percent_;
  switch (current_network_mode_) {
    case M_MODEM_TECH_GSM:
      result.gsm_rssi = GetValueInRange(kRssiRange, percent);
      break;
    case M_MODEM_TECH_CDMA:
      result.cdma_dbm = GetValueInRange(kDbmRange, percent) * -1;
      break;
    case M_MODEM_TECH_EVDO:
      result.evdo_dbm = GetValueInRange(kDbmRange, percent) * -1;
      break;
    case M_MODEM_TECH_LTE:
      result.lte_rsrp = GetValueInRange(kRsrpRange, percent) * -1;
      break;
    case M_MODEM_TECH_WCDMA:
      result.wcdma_rssi = GetValueInRange(kRssiRange, percent);
      break;
    case M_MODEM_TECH_NR:
      // special for NR: it uses LTE as primary, so LTE signal strength is
      // needed as well
      result.lte_rsrp = GetValueInRange(kRsrpRange, percent) * -1;
      result.nr_ss_rsrp = GetValueInRange(kRsrpRange, percent) * -1;
      break;
    default:
      break;
  }
  return result;
}

/* AT+REMOTEREG: state*/
void NetworkService::HandleReceiveRemoteVoiceDataReg(const Client& client,
                                                     std::string& command) {
  (void)client;
  std::stringstream ss;
  std::string states = command.substr(std::string("AT+REMOTEREG:").size());
  int stated = std::stoi(states, nullptr, 10);

  UpdateRegisterState(NET_REGISTRATION_UNREGISTERED);

  thread_looper_->Post(
      makeSafeCallback(this, &NetworkService::UpdateRegisterState,
                       (cuttlefish::NetworkService::RegistrationState)stated),
      std::chrono::seconds(1));
}

/* AT+REMOTECTEC: ctec */
void NetworkService::HandleReceiveRemoteCTEC(const Client& client,
                                             std::string& command) {
  (void)client;
  LOG(DEBUG) << "calling ctec from remote";
  std::stringstream ss;
  std::string types = command.substr(std::string("AT+REMOTECTEC: ").size());
  int preferred_mask_new = std::stoi(types, nullptr, 10);

  if (preferred_mask_new != preferred_network_mode_) {
    preferred_network_mode_ = preferred_mask_new;
  }
  auto current_network_mode_new =
      (ModemTechnology)getModemTechFromPrefer(preferred_mask_new);
  if (current_network_mode_new != current_network_mode_) {
    current_network_mode_ = current_network_mode_new;
    auto saved_state = voice_registration_status_.registration_state;
    UpdateRegisterState(NET_REGISTRATION_UNREGISTERED);

    ss << "+CTEC: " << current_network_mode_;

    thread_looper_->Post(
        makeSafeCallback(this, &NetworkService::UpdateRegisterState,
                         saved_state),
        std::chrono::seconds(1));
  }
}

/* AT+REMOTESIGNAL: percent */
void NetworkService::HandleReceiveRemoteSignal(const Client& client,
                                               std::string& command) {
  (void)client;
  std::stringstream ss;
  std::string percents = command.substr(std::string("AT+REMOTESIGNAL:").size());
  int percent = std::stoi(percents, nullptr, 10);

  if (percent >= 0 && percent <= 100) {
    signal_strength_percent_ = percent;
  } else {
    LOG(DEBUG) << "out of bound signal strength percent: " << percent;
    return;
  }

  OnSignalStrengthChanged();
}

void NetworkService::OnSignalStrengthChanged() {
  SendUnsolicitedCommand(BuildCSQCommandResponse(GetCurrentSignalStrength()));
}

NetworkService::RegistrationState NetworkService::GetVoiceRegistrationState() const {
  return voice_registration_status_.registration_state;
}

NetworkService::KeepSignalStrengthChangingLoop::KeepSignalStrengthChangingLoop(
    NetworkService& network_service)
    : network_service_{network_service}, loop_started_ ATOMIC_FLAG_INIT {}

void NetworkService::KeepSignalStrengthChangingLoop::Start() {
  if (loop_started_.test_and_set()) {
    LOG(ERROR) << "Signal strength is already changing automatically";
  } else {
    UpdateSignalStrengthCallback();
  }
}

void NetworkService::KeepSignalStrengthChangingLoop::
    UpdateSignalStrengthCallback() {
  if (network_service_.IsHasNetwork()) {
    network_service_.signal_strength_percent_ -= 5;
    // With "close to 0" values, the signal strength bar on the Android UI will
    // be shown empty, this also represents that theres's no connectivity which
    // is missleading as the connectivity continues, so a lower bound of 10 will
    // be used so the signal strenght bar is never emptied
    if (network_service_.signal_strength_percent_ <= 10) {
      network_service_.signal_strength_percent_ = 100;
    }
    network_service_.OnSignalStrengthChanged();
  }
  network_service_.thread_looper_->Post(
      makeSafeCallback(this, &NetworkService::KeepSignalStrengthChangingLoop::
                                 UpdateSignalStrengthCallback),
      std::chrono::seconds(10));
}

}  // namespace cuttlefish
