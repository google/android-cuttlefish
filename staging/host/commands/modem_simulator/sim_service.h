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

#include <tinyxml2.h>

#include "modem_service.h"

namespace cuttlefish {

using namespace tinyxml2;

class NetworkService;

class SimService : public ModemService, public std::enable_shared_from_this<SimService> {
 public:
  SimService(int32_t service_id, ChannelMonitor* channel_monitor,
             ThreadLooper* thread_looper);
  ~SimService() = default;

  SimService(const SimService &) = delete;
  SimService &operator=(const SimService &) = delete;

  void SetupDependency(NetworkService* net);

  void HandleSIMStatusReq(const Client& client);
  void HandleChangeOrEnterPIN(const Client& client, const std::string& command);
  void HandleSIM_IO(const Client& client, const std::string& command);
  void HandleGetIMSI(const Client& client);
  void HandleGetIccId(const Client& client);
  void HandleFacilityLock(const Client& client, const std::string& command);
  void HandleOpenLogicalChannel(const Client& client,
                                const std::string& command);
  void HandleCloseLogicalChannel(const Client& client,
                                 const std::string& command);
  void HandleTransmitLogicalChannel(const Client& client,
                                    const std::string& command);
  void HandleChangePassword(const Client& client, const std::string& command);
  void HandleQueryRemainTimes(const Client& client, const std::string& command);
  void HandleCdmaSubscriptionSource(const Client& client,
                                    const std::string& command);
  void HandleCdmaRoamingPreference(const Client& client,
                                   const std::string& command);

  void SavePinStateToIccProfile();
  void SaveFacilityLockToIccProfile();
  bool IsFDNEnabled();
  bool IsFixedDialNumber(std::string_view number);
  XMLElement* GetIccProfile();
  std::string GetPhoneNumber();

  enum SimStatus {
    SIM_STATUS_ABSENT = 0,
    SIM_STATUS_NOT_READY,
    SIM_STATUS_READY,
    SIM_STATUS_PIN,
    SIM_STATUS_PUK,
  };

  SimStatus GetSimStatus() const;
  std::string GetSimOperator();

 private:
  void InitializeServiceState();
  std::vector<CommandHandler> InitializeCommandHandlers();
  void InitializeSimFileSystemAndSimState();
  void InitializeFacilityLock();
  void OnSimStatusChanged();

  NetworkService* network_service_;

  /* SimStatus */
  SimStatus sim_status_;

  /* SimFileSystem */
  struct SimFileSystem {
    enum EFId: int32_t {
      EF_ADN = 0x6F3A,
      EF_FDN = 0x6F3B,
      EF_GID1 = 0x6F3E,
      EF_GID2 = 0x6F3F,
      EF_SDN = 0x6F49,
      EF_EXT1 = 0x6F4A,
      EF_EXT2 = 0x6F4B,
      EF_EXT3 = 0x6F4C,
      EF_EXT5 = 0x6F4E,
      EF_EXT6 = 0x6FC8,   // Ext record for EF[MBDN]
      EF_MWIS = 0x6FCA,
      EF_MBDN = 0x6FC7,
      EF_PNN = 0x6FC5,
      EF_OPL = 0x6FC6,
      EF_SPN = 0x6F46,
      EF_SMS = 0x6F3C,
      EF_ICCID = 0x2FE2,
      EF_AD = 0x6FAD,
      EF_MBI = 0x6FC9,
      EF_MSISDN = 0x6F40,
      EF_SPDI = 0x6FCD,
      EF_SST = 0x6F38,
      EF_CFIS = 0x6FCB,
      EF_IMG = 0x4F20,

      // USIM SIM file ids from TS 131.102
      EF_PBR = 0x4F30,
      EF_LI = 0x6F05,

      // GSM SIM file ids from CPHS (phase 2, version 4.2) CPHS4_2.WW6
      EF_MAILBOX_CPHS = 0x6F17,
      EF_VOICE_MAIL_INDICATOR_CPHS = 0x6F11,
      EF_CFF_CPHS = 0x6F13,
      EF_SPN_CPHS = 0x6F14,
      EF_SPN_SHORT_CPHS = 0x6F18,
      EF_INFO_CPHS = 0x6F16,
      EF_CSP_CPHS = 0x6F15,

      // CDMA RUIM file ids from 3GPP2 C.S0023-0
      EF_CST = 0x6F32,
      EF_RUIM_SPN =0x6F41,

      // ETSI TS.102.221
      EF_PL = 0x2F05,
      // 3GPP2 C.S0065
      EF_CSIM_LI = 0x6F3A,
      EF_CSIM_SPN =0x6F41,
      EF_CSIM_MDN = 0x6F44,
      EF_CSIM_IMSIM = 0x6F22,
      EF_CSIM_CDMAHOME = 0x6F28,
      EF_CSIM_EPRL = 0x6F5A,
      EF_CSIM_MIPUPP = 0x6F4D,

      //ISIM access
      EF_IMPU = 0x6F04,
      EF_IMPI = 0x6F02,
      EF_DOMAIN = 0x6F03,
      EF_IST = 0x6F07,
      EF_PCSCF = 0x6F09,
      EF_PSI = 0x6FE5,

      //PLMN Selection Information w/ Access Technology TS 131.102
      EF_PLMN_W_ACT = 0x6F60,
      EF_OPLMN_W_ACT = 0x6F61,
      EF_HPLMN_W_ACT = 0x6F62,

      //Equivalent Home and Forbidden PLMN Lists TS 131.102
      EF_EHPLMN = 0x6FD9,
      EF_FPLMN = 0x6F7B,

      // Last Roaming Selection Indicator
      EF_LRPLMNSI = 0x6FDC,

      //Search interval for higher priority PLMNs
      EF_HPPLMN = 0x6F31,
    };

    XMLElement* GetRootElement();

    static std::string GetCommonIccEFPath(EFId efid);
    static std::string GetUsimEFPath(EFId efid);

    static XMLElement *FindAttribute(XMLElement* parent,
                                     const std::string& attr_name,
                                     const std::string& attr_value);

    XMLElement* AppendNewElement(XMLElement* parent, const char* name);
    XMLElement* AppendNewElementWithText(XMLElement* parent, const char* name,
                                         const char* text);

    XMLDocument doc;
    std::string file_path;
  };
  SimFileSystem sim_file_system_;


  /* PinStatus */
  struct PinStatus {
    enum ChangeMode {WITH_PIN, WITH_PUK};

    std::string pin_;
    std::string puk_;
    int pin_remaining_times_;
    int puk_remaining_times_;

    bool CheckPasswordValid(std::string_view password);

    bool VerifyPIN(const std::string_view pin);
    bool VerifyPUK(const std::string_view puk);
    bool ChangePIN(ChangeMode mode, const std::string_view pin_or_puk,
                   const std::string_view new_pin);
    bool ChangePUK(const std::string_view puk, const std::string_view new_puk);
  };
  PinStatus pin1_status_;
  PinStatus pin2_status_;

  bool checkPin1AndAdjustSimStatus(std::string_view password);
  bool ChangePin1AndAdjustSimStatus(PinStatus::ChangeMode mode,
                                    std::string_view pin,
                                    std::string_view new_pin);

  /*  FacilityLock */
  struct FacilityLock {
    enum LockType {
      AO = 1,  // Barr all outgoing calls
      OI = 2,  // Barr all outgoing international calls
      OX = 3,  // Barr all outgoing international calls, except to Home Country
      AI = 4,  // Barr all incoming calls
      IR = 5,  // Barr all call, when roaming outside Home Country
      AB = 6,  // All barring services
      AG = 7,  // All outgoing barring services
      AC = 8,  // All incoming barring services
      SC = 9,  // PIN enable/disable
      FD = 10,  // SIM fixed FDN dialing lock, PIN2 is required as a password
    };

    enum Mode {
      UNLOCK = 0,
      LOCK = 1,
      QUERY = 2,
    };

    enum Class : int32_t {
      DEFAULT = 7,      // all classes
      VOICE = 1 << 0,   // telephony
      DATA = 1 << 1,    // to all bear service
      FAX = 1 << 2,     // facsimile services
      SMS = 1 << 3,     // short message services
    };

    enum LockStatus {
      DISABLE,
      ENABLE,
    };

    LockStatus lock_status;  // Ignore class

    FacilityLock(LockStatus status) : lock_status(status) {}
  };
  std::map<std::string, FacilityLock> facility_lock_;

  /* LogicalChannel */
  struct LogicalChannel {
    std::string df_name;
    bool is_open;
    int session_id;

    LogicalChannel(int session_id) :
      df_name(""), is_open(false), session_id(session_id) {};
  };
  std::vector<LogicalChannel> logical_channels_;

  int cdma_subscription_source_;
  int cdma_roaming_preference_;
};

}  // namespace cuttlefish
