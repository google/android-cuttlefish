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

#include "modem_service.h"

namespace cuttlefish {

class SupService : public ModemService, public std::enable_shared_from_this<SupService> {
 public:
  SupService(int32_t service_id, ChannelMonitor* channel_monitor,
             ThreadLooper* thread_looper);
  ~SupService() = default;

  SupService(const SupService &) = delete;
  SupService &operator=(const SupService &) = delete;

  void HandleUSSD(const Client& client, std::string& command);
  void HandleCLIR(const Client& client, std::string& command);
  void HandleCallWaiting(const Client& client, std::string& command);
  void HandleCLIP(const Client& client);
  void HandleCallForward(const Client& client, std::string& command);
  void HandleSuppServiceNotifications(const Client& client, std::string& command);

 private:
  std::vector<CommandHandler> InitializeCommandHandlers();
  void InitializeServiceState();

  struct ClirStatusInfo {
    enum ClirType {
      DEFAULT = 0,                  // "use subscription default value"
      CLIR_INVOCATION        = 1,   // restrict CLI presentation
      CLIR_SUPPRESSION       = 2,   // allow CLI presentation
    };

    enum ClirStatus {
      CLIR_NOT_PROVISIONED         = 0,
      CLIR_PROVISIONED             = 1,
      UNKNOWN                       = 2,
      CLIR_PRESENTATION_RESTRICTED = 3,
      CLIR_PRESENTATION_ALLOWED    = 4,
    };

    ClirType type;
    ClirStatus status;
  };
  ClirStatusInfo clir_status_;

  struct CallForwardInfo {
    enum CallForwardInfoStatus {
      DISABLE       = 0,
      ENABLE        = 1,
      INTERROGATE   = 2,
      REGISTRATION  = 3,
      ERASURE       = 4,
    };

    enum Reason {
      CFU         = 0,  // communication forwarding unconditional
      CFB         = 1,  //communication forwarding on busy user
      CFNR        = 2,  // communication forwarding on no reply
      CFNRC       = 3,  // communication forwarding on subscriber not reachable
      ALL_CF      = 4,  // all call forwarding
      ALL_CONDITIONAL_CF = 5, //all conditional call forwarding
      CD          = 6,  // communication deflection
      CFNL        = 7,  // communication forwarding on not logged-in
    };

    CallForwardInfoStatus status;
    Reason reason;
    int number_type;    // From 27.007 +CCFC/+CLCK "class"
    int ton;            // "type" from TS 27.007 7.11
    std::string number; // "number" from TS 27.007 7.11. May be NULL
    int timeSeconds;    // for CF no reply only

    CallForwardInfo(Reason reason) :
      status(DISABLE), reason(reason), number_type(2), ton(129), number(""),
        timeSeconds(0){};
  };
  std::vector<CallForwardInfo> call_forward_infos_;

  struct CallWaitingInfo {
    int presentation_status;  // sets / shows the result code presentation status to the TE,
                              // 0: disable; 1: enable
    int mode;                 // 0: disable; 1: enable; 2: query status
    int classx;               // a sum of integers each representing a class of information
                              // default 7-voice, data and fax, see FacilityLock::Class

    CallWaitingInfo() :
      presentation_status(1), mode(0), classx(7) {};
  };
  CallWaitingInfo call_waiting_info_;
};

}  // namespace cuttlefish
