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

#include <ctime>

#include "host/commands/modem_simulator/data_service.h"
#include "host/commands/modem_simulator/misc_service.h"
#include "host/commands/modem_simulator/modem_service.h"
#include "host/commands/modem_simulator/sim_service.h"

namespace cuttlefish {

class NetworkService : public ModemService, public std::enable_shared_from_this<NetworkService> {
 public:
  NetworkService(int32_t service_id_, ChannelMonitor* channel_monitor,
                 ThreadLooper* thread_looper);
  ~NetworkService() = default;

  NetworkService(const NetworkService &) = delete;
  NetworkService &operator=(const NetworkService &) = delete;

  void SetupDependency(MiscService* misc, SimService* sim, DataService* data);

  void HandleRadioPowerReq(const Client& client);
  void HandleRadioPower(const Client& client, std::string& command);
  void HandleSignalStrength(const Client& client);
  void HandleQueryNetworkSelectionMode(const Client& client);
  void HandleRequestOperator(const Client& client);
  void HandleQueryAvailableNetwork(const Client& client);
  void HandleSetNetworkSelectionMode(const Client& client, std::string& command);
  void HandleVoiceNetworkRegistration(const Client& client, std::string& command);
  void HandleDataNetworkRegistration(const Client& client, std::string& command);
  void HandleGetPreferredNetworkType(const Client& client);
  void HandleQuerySupportedTechs(const Client& client);
  void HandleSetPreferredNetworkType(const Client& client, std::string& command);
  void HandleNetworkRegistration(cuttlefish::SharedFD client, std::string& command);

  void HandleReceiveRemoteVoiceDataReg(const Client& client,
                                       std::string& command);
  void HandleReceiveRemoteCTEC(const Client& client, std::string& command);
  void HandleReceiveRemoteSignal(const Client& client, std::string& command);

  void OnSimStatusChanged(SimService::SimStatus sim_status);
  void OnVoiceRegisterStateChanged();
  void OnDataRegisterStateChanged();
  void OnSignalStrengthChanged();

  enum RegistrationState {
    NET_REGISTRATION_UNREGISTERED = 0,
    NET_REGISTRATION_HOME         = 1,
    NET_REGISTRATION_SEARCHING    = 2,
    NET_REGISTRATION_DENIED       = 3,
    NET_REGISTRATION_UNKNOWN      = 4,
    NET_REGISTRATION_ROAMING      = 5,
    NET_REGISTRATION_EMERGENCY    = 8
  };
  RegistrationState GetVoiceRegistrationState() const;

 private:
  void InitializeServiceState();
  std::vector<CommandHandler> InitializeCommandHandlers();
  void InitializeNetworkOperator();
  void InitializeSimOperator();

  bool WakeupFromSleep();
  bool IsHasNetwork();
  void UpdateRegisterState(RegistrationState state);
  void AdjustSignalStrengthValue(int& value, const std::pair<int, int>& range);
  void SetSignalStrengthValue(int& value, const std::pair<int, int>& range,
                              double percentd);
  std::string GetSignalStrength();

  MiscService* misc_service_ = nullptr;
  SimService* sim_service_ = nullptr;
  DataService* data_service_ = nullptr;

  enum RadioState : int32_t {
    RADIO_STATE_OFF,
    RADIO_STATE_ON,
  };
  RadioState radio_state_;

  /* Operator */
  struct NetworkOperator {
    enum OperatorState {
      OPER_STATE_UNKNOWN    = 0,
      OPER_STATE_AVAILABLE  = 1,
      OPER_STATE_CURRENT    = 2,
      OPER_STATE_FORBIDDEN  = 3
    };

    std::string numeric;
    std::string long_name;
    std::string short_name;
    OperatorState operator_state;

    NetworkOperator() {}

    NetworkOperator(const std::string& number,
                    const std::string& ln,
                    const std::string& sn,
                    OperatorState state)
        : numeric(number),
          long_name(ln),
          short_name(sn),
          operator_state(state) {}
  };

  enum OperatorSelectionMode {
    OPER_SELECTION_AUTOMATIC = 0,
    OPER_SELECTION_MANUAL,
    OPER_SELECTION_DEREGISTRATION,
    OPER_SELECTION_SET_FORMAT,
    OPER_SELECTION_MANUAL_AUTOMATIC
  };

  std::vector<NetworkOperator> operator_list_;
  std::string current_operator_numeric_ = "";
  OperatorSelectionMode oper_selection_mode_;

  /* SignalStrength */
  struct SignalStrength {
    int gsm_rssi;  /* Valid values are (0-31, 99) as defined in TS 27.007 8.5 */
    int gsm_ber;   /* bit error rate (0-7, 99) as defined in TS 27.007 8.5 */

    int cdma_dbm;   /* Valid values are positive integers.  This value is the actual RSSI value
                     * multiplied by -1.  Example: If the actual RSSI is -75, then this response
                     * value will be 75.
                     */
    int cdma_ecio;  /* Valid values are positive integers.  This value is the actual Ec/Io multiplied
                     * by -10.  Example: If the actual Ec/Io is -12.5 dB, then this response value
                     * will be 125.
                     */

    int evdo_dbm;   /* Refer cdma_dbm */
    int evdo_ecio;  /* Refer cdma_ecio */
    int evdo_snr;   /* Valid values are 0-8.  8 is the highest signal to noise ratio. */

    int lte_rssi;   /* Refer gsm_rssi */
    int lte_rsrp;   /* The current Reference Signal Receive Power in dBm multiplied by -1.
                     * Range: 44 to 140 dBm
                     * INT_MAX: 0x7FFFFFFF denotes invalid value.
                     * Reference: 3GPP TS 36.133 9.1.4 */
    int lte_rsrq;   /* The current Reference Signal Receive Quality in dB multiplied by -1.
                     * Range: 20 to 3 dB.
                     * INT_MAX: 0x7FFFFFFF denotes invalid value.
                     * Reference: 3GPP TS 36.133 9.1.7 */
    int lte_rssnr;  /* The current reference signal signal-to-noise ratio in 0.1 dB units.
                     * Range: -200 to +300 (-200 = -20.0 dB, +300 = 30dB).
                     * INT_MAX : 0x7FFFFFFF denotes invalid value.
                     * Reference: 3GPP TS 36.101 8.1.1 */
    int lte_cqi;    /* The current Channel Quality Indicator.
                     * Range: 0 to 15.
                     * INT_MAX : 0x7FFFFFFF denotes invalid value.
                     * Reference: 3GPP TS 36.101 9.2, 9.3, A.4 */
    int lte_ta;     /* timing advance in micro seconds for a one way trip from cell to device.
                     * Approximate distance can be calculated using 300m/us * timingAdvance.
                     * Range: 0 to 0x7FFFFFFE
                     * INT_MAX : 0x7FFFFFFF denotes invalid value.
                     * Reference: 3GPP 36.321 section 6.1.3.5 */

    int tdscdma_rscp;   /* P-CCPCH RSCP as defined in TS 25.225 5.1.1
                         * Valid values are (0-96, 255) as defined in TS 27.007 8.69
                         * INT_MAX denotes that the value is invalid/unreported. */

    int wcdma_rssi;  /* Refer gsm_rssi */
    int wcdma_ber;   /* Refer gsm_ber */

    int32_t nr_ss_rsrp;   /* SS reference signal received power, multiplied by -1.
                           * Reference: 3GPP TS 38.215.
                           * Range [44, 140], INT_MAX means invalid/unreported. */
    int32_t nr_ss_rsrq;   /* SS reference signal received quality, multiplied by -1.
                           * Reference: 3GPP TS 38.215.
                           * Range [3, 20], INT_MAX means invalid/unreported. */
    int32_t nr_ss_sinr;   /* SS signal-to-noise and interference ratio.
                           * Reference: 3GPP TS 38.215 section 5.1.*, 3GPP TS 38.133 section 10.1.16.1.
                           * Range [-23, 40], INT_MAX means invalid/unreported. */
    int32_t nr_csi_rsrp;  /* CSI reference signal received power, multiplied by -1.
                           * Reference: 3GPP TS 38.215.
                           * Range [44, 140], INT_MAX means invalid/unreported. */
    int32_t nr_csi_rsrq;  /* CSI reference signal received quality, multiplied by -1.
                           * Reference: 3GPP TS 38.215.
                           * Range [3, 20], INT_MAX means invalid/unreported. */
    int32_t nr_csi_sinr;  /* CSI signal-to-noise and interference ratio.
                           * Reference: 3GPP TS 138.215 section 5.1.*, 3GPP TS 38.133 section 10.1.16.1.
                           * Range [-23, 40], INT_MAX means invalid/unreported. */

    // Default invalid value
    SignalStrength():
      gsm_rssi(99),     // [0, 31]
      gsm_ber(0),       // [7, 99]
      cdma_dbm(125),    // [0, 120]
      cdma_ecio(165),   // [0, 160]
      evdo_dbm(125),    // [0, 120]
      evdo_ecio(165),   // [0, 160]
      evdo_snr(-1),     // [0, 8]
      lte_rssi(99),     // [0, 31]
      lte_rsrp(-1),     // [43,140]
      lte_rsrq(-5),     // [-3,34]
      lte_rssnr(-205),  // [-200, 300]
      lte_cqi(-1),      // [0, 15]
      lte_ta(-1),       // [0, 1282]
      tdscdma_rscp(99), // [0, 96]
      wcdma_rssi(99),   // [0, 31]
      wcdma_ber(0),     // [7, 99]
      nr_ss_rsrp(0),    // [44, 140]
      nr_ss_rsrq(0),    // [3, 10]
      nr_ss_sinr(45),   // [-23,40]
      nr_csi_rsrp(0),   // [44, 140]
      nr_csi_rsrq(0),   // [3, 20]
      nr_csi_sinr(30)   // [-23, 23]
      {}

    // After radio power on, off, or set network mode, reset to invalid value
    void Reset() {
      gsm_rssi = 99;
      gsm_ber = 0;
      cdma_dbm = 125;
      cdma_ecio = 165;
      evdo_dbm = 125;
      evdo_ecio = 165;
      evdo_snr = -1;
      lte_rssi = 99;
      lte_rsrp = -1;
      lte_rsrq = -5;
      lte_rssnr = -205;
      lte_cqi = -1;
      lte_ta = -1;
      tdscdma_rscp = 99;
      wcdma_rssi = 99;
      wcdma_ber = 0;
      nr_ss_rsrp = 0;
      nr_ss_rsrq = 0;
      nr_ss_sinr = 45;
      nr_csi_rsrp = 0;
      nr_csi_rsrq = 0;
      nr_csi_sinr = 30;
    }
  };

  SignalStrength signal_strength_;

  /* Data / voice Registration State */
  struct NetworkRegistrationStatus {
    enum RegistrationUnsolMode {
      REGISTRATION_UNSOL_DISABLED     = 0,
      REGISTRATION_UNSOL_ENABLED      = 1,
      REGISTRATION_UNSOL_ENABLED_FULL = 2
    };

    enum AccessTechnoloy {
      ACESS_TECH_GSM          = 0,
      ACESS_TECH_GSM_COMPACT  = 1,
      ACESS_TECH_UTRAN        = 2,
      ACESS_TECH_EGPRS        = 3,
      ACESS_TECH_HSDPA        = 4,
      ACESS_TECH_HSUPA        = 5,
      ACESS_TECH_HSPA         = 6,
      ACESS_TECH_EUTRAN       = 7,
      ACESS_TECH_EC_GSM_IoT   = 8,
      ACESS_TECH_E_UTRAN      = 9,
      ACESS_TECH_E_UTRA       = 10,
      ACESS_TECH_NR           = 11,
      ACESS_TECH_NG_RAN       = 12,
      ACESS_TECH_E_UTRA_NR    = 13
    };

    NetworkRegistrationStatus() :
      unsol_mode(REGISTRATION_UNSOL_ENABLED_FULL),
      registration_state(NET_REGISTRATION_UNREGISTERED),
      network_type(ACESS_TECH_EUTRAN) {}

    RegistrationUnsolMode unsol_mode;
    RegistrationState registration_state;
    AccessTechnoloy network_type;
  };

  NetworkRegistrationStatus voice_registration_status_;
  NetworkRegistrationStatus data_registration_status_;

  enum ModemTechnology {
    M_MODEM_TECH_GSM    = 1 << 0,
    M_MODEM_TECH_WCDMA  = 1 << 1,
    M_MODEM_TECH_CDMA   = 1 << 2,
    M_MODEM_TECH_EVDO   = 1 << 3,
    M_MODEM_TECH_TDSCDMA= 1 << 4,
    M_MODEM_TECH_LTE    = 1 << 5,
    M_MODEM_TECH_NR     = 1 << 6,
  };
  ModemTechnology current_network_mode_;
  int preferred_network_mode_;
  int modem_radio_capability_;

  NetworkRegistrationStatus::AccessTechnoloy getNetworkTypeFromTech(ModemTechnology modemTech);
  int getModemTechFromPrefer(int preferred_mask);
  ModemTechnology getTechFromNetworkType(NetworkRegistrationStatus::AccessTechnoloy act);

  bool first_signal_strength_request_;  // For time update
  time_t android_last_signal_time_;
};

}  // namespace cuttlefish
