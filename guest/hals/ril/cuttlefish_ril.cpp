/*
** Copyright 2017, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License ioogle/s distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include "guest/hals/ril/cuttlefish_ril.h"

#include <cutils/properties.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "common/libs/device_config/device_config.h"
#include "common/libs/net/netlink_client.h"
#include "common/libs/net/network_interface.h"
#include "common/libs/net/network_interface_manager.h"

#define CUTTLEFISH_RIL_VERSION_STRING "Android Cuttlefish RIL 1.4"

/* Modem Technology bits */
#define MDM_GSM 0x01
#define MDM_WCDMA 0x02
#define MDM_CDMA 0x04
#define MDM_EVDO 0x08
#define MDM_LTE 0x10

typedef enum {
  SIM_ABSENT = 0,
  SIM_NOT_READY = 1,
  SIM_READY = 2,  // SIM_READY means the radio state is RADIO_STATE_SIM_READY
  SIM_PIN = 3,
  SIM_PUK = 4,
  SIM_NETWORK_PERSONALIZATION = 5,
  RUIM_ABSENT = 6,
  RUIM_NOT_READY = 7,
  RUIM_READY = 8,
  RUIM_PIN = 9,
  RUIM_PUK = 10,
  RUIM_NETWORK_PERSONALIZATION = 11
} SIM_Status;

static std::unique_ptr<cuttlefish::DeviceConfig> global_ril_config = nullptr;

static const struct RIL_Env* gce_ril_env;

static const struct timeval TIMEVAL_SIMPOLL = {3, 0};

static time_t gce_ril_start_time;

static void pollSIMState(void* param);

RIL_RadioState gRadioPowerState = RADIO_STATE_OFF;
RIL_RadioAccessFamily default_access = RAF_LTE;

struct DataCall {
  enum AllowedAuthenticationType { kNone = 0, kPap = 1, kChap = 2, kBoth = 3 };

  enum ConnectionType {
    kConnTypeIPv4,
    kConnTypeIPv6,
    kConnTypeIPv4v6,
    kConnTypePPP
  };

  enum LinkState {
    kLinkStateInactive = 0,
    kLinkStateDown = 1,
    kLinkStateUp = 2,
  };

  RIL_RadioTechnology technology_;
  RIL_DataProfile profile_;
  std::string access_point_;
  std::string username_;
  std::string password_;
  AllowedAuthenticationType auth_type_;
  ConnectionType connection_type_;
  LinkState link_state_;
  RIL_DataCallFailCause fail_cause_;
  std::string other_properties_;
};

static std::string gSimPIN = "0000";
static const std::string gSimPUK = "11223344";
static int gSimPINAttempts = 0;
static const int gSimPINAttemptsMax = 3;
static SIM_Status gSimStatus = SIM_NOT_READY;
static bool areUiccApplicationsEnabled = true;

// SetUpNetworkInterface configures IP and Broadcast addresses on a RIL
// controlled network interface.
// This call returns true, if operation was successful.
bool SetUpNetworkInterface(const char* ipaddr, int prefixlen,
                           const char* bcaddr) {
  auto factory = cuttlefish::NetlinkClientFactory::Default();
  std::unique_ptr<cuttlefish::NetlinkClient> nl(factory->New(NETLINK_ROUTE));
  std::unique_ptr<cuttlefish::NetworkInterfaceManager> nm(
      cuttlefish::NetworkInterfaceManager::New(factory));
  std::unique_ptr<cuttlefish::NetworkInterface> ni(nm->Open("rmnet0", "eth1"));

  if (ni) {
    ni->SetName("rmnet0");
    ni->SetAddress(ipaddr);
    ni->SetBroadcastAddress(bcaddr);
    ni->SetPrefixLength(prefixlen);
    ni->SetOperational(true);
    bool res = nm->ApplyChanges(*ni);
    if (!res) ALOGE("Could not configure rmnet0");
    return res;
  }
  return false;
}

// TearDownNetworkInterface disables network interface.
// This call returns true, if operation was successful.
bool TearDownNetworkInterface() {
  auto nm(cuttlefish::NetworkInterfaceManager::New(nullptr));
  auto ni(nm->Open("rmnet0", "eth1"));

  if (ni) {
    ni->SetOperational(false);
    bool res = nm->ApplyChanges(*ni);
    if (!res) ALOGE("Could not disable rmnet0");
    return res;
  }
  return false;
}

static int gNextDataCallId = 8;
static std::map<int, DataCall> gDataCalls;
static bool gRilConnected = false;

static int request_or_send_data_calllist(RIL_Token* t) {
  RIL_Data_Call_Response_v11* responses =
      new RIL_Data_Call_Response_v11[gDataCalls.size()];

  int index = 0;

  ALOGV("Query data call list: %zu data calls tracked.", gDataCalls.size());

  for (std::map<int, DataCall>::iterator iter = gDataCalls.begin();
       iter != gDataCalls.end(); ++iter, ++index) {
    responses[index].status = iter->second.fail_cause_;
    responses[index].suggestedRetryTime = -1;
    responses[index].cid = iter->first;
    responses[index].active = iter->second.link_state_;

    switch (iter->second.connection_type_) {
      case DataCall::kConnTypeIPv4:
        responses[index].type = (char*)"IP";
        break;
      case DataCall::kConnTypeIPv6:
        responses[index].type = (char*)"IPV6";
        break;
      case DataCall::kConnTypeIPv4v6:
        responses[index].type = (char*)"IPV4V6";
        break;
      case DataCall::kConnTypePPP:
        responses[index].type = (char*)"PPP";
        break;
      default:
        responses[index].type = (char*)"IP";
        break;
    }

    responses[index].ifname = (char*)"rmnet0";
    responses[index].addresses =
      const_cast<char*>(global_ril_config->ril_address_and_prefix());
    responses[index].dnses = const_cast<char*>(global_ril_config->ril_dns());
    responses[index].gateways = const_cast<char*>(global_ril_config->ril_gateway());
    responses[index].pcscf = (char*)"";
    responses[index].mtu = 1440;
  }

  bool new_conn_state = (gDataCalls.size() > 0);

  if (gRilConnected != new_conn_state) {
    time_t curr_time;
    time(&curr_time);
    double diff_in_secs = difftime(curr_time, gce_ril_start_time);

    gRilConnected = new_conn_state;

    if (new_conn_state) {
      ALOGV("MOBILE_DATA_CONNECTED %.2lf seconds", diff_in_secs);
    } else {
      ALOGV("MOBILE_DATA_DISCONNECTED %.2lf seconds", diff_in_secs);
    }

    if (property_set("ril.net_connected", new_conn_state ? "1" : "0")) {
      ALOGE("Couldn't set a system property ril.net_connected.");
    }
  }

  if (t != NULL) {
    gce_ril_env->OnRequestComplete(*t, RIL_E_SUCCESS, responses,
                                   gDataCalls.size() * sizeof(*responses));
  } else {
    gce_ril_env->OnUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED,
                                       responses,
                                       gDataCalls.size() * sizeof(*responses));
  }
  delete[] responses;
  return 0;
}

static void request_datacall_fail_cause(RIL_Token t) {
  RIL_DataCallFailCause fail = PDP_FAIL_DATA_REGISTRATION_FAIL;

  if (gDataCalls.size() > 0) {
    fail = gDataCalls.rbegin()->second.fail_cause_;
  }

  ALOGV("Requesting last data call setup fail cause (%d)", fail);
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &fail, sizeof(fail));
};

static void request_data_calllist(void* /*data*/, size_t /*datalen*/,
                                  RIL_Token t) {
  request_or_send_data_calllist(&t);
}

static void request_setup_data_call(void* data, size_t datalen, RIL_Token t) {
  char** details = static_cast<char**>(data);
  const size_t fields = datalen / sizeof(details[0]);

  // There are two different versions of this interface, one providing 7 strings
  // and the other providing 8. The code below will assume the presence of 7
  // strings in all cases, so bail out here if things appear to be wrong. We
  // protect the 8 string case below.
  if (fields < 7) {
    ALOGE("%s returning: called with small datalen %zu", __FUNCTION__, datalen);
    return;
  }

  DataCall call;
  int tech = atoi(details[0]);
  switch (tech) {
    case 0:
    case 2 + RADIO_TECH_1xRTT:
      call.technology_ = RADIO_TECH_1xRTT;
      break;

    case 1:
    case 2 + RADIO_TECH_EDGE:
      call.technology_ = RADIO_TECH_EDGE;
      break;

    default:
      call.technology_ = RIL_RadioTechnology(tech - 2);
      break;
  }

  int profile = atoi(details[1]);
  call.profile_ = RIL_DataProfile(profile);

  if (details[2]) call.access_point_ = details[2];
  if (details[3]) call.username_ = details[3];
  if (details[4]) call.password_ = details[4];

  int auth_type = atoi(details[5]);
  call.auth_type_ = DataCall::AllowedAuthenticationType(auth_type);

  if (!strcmp("IP", details[6])) {
    call.connection_type_ = DataCall::kConnTypeIPv4;
  } else if (!strcmp("IPV6", details[6])) {
    call.connection_type_ = DataCall::kConnTypeIPv6;
  } else if (!strcmp("IPV4V6", details[6])) {
    call.connection_type_ = DataCall::kConnTypeIPv4v6;
  } else if (!strcmp("PPP", details[6])) {
    call.connection_type_ = DataCall::kConnTypePPP;
  } else {
    ALOGW("Unknown / unsupported connection type %s. Falling back to IPv4",
          details[6]);
    call.connection_type_ = DataCall::kConnTypeIPv4;
  }

  if (call.connection_type_ != DataCall::kConnTypeIPv4) {
    ALOGE("Non-IPv4 connections are not supported by Cuttlefish RIL.");
    gce_ril_env->OnRequestComplete(t, RIL_E_INVALID_ARGUMENTS, NULL, 0);
    return;
  }

  call.link_state_ = DataCall::kLinkStateUp;
  call.fail_cause_ = PDP_FAIL_NONE;
  if (fields > 7) {
    if (details[7]) call.other_properties_ = details[7];
  }

  if (gDataCalls.empty()) {
    SetUpNetworkInterface(global_ril_config->ril_ipaddr(),
                          global_ril_config->ril_prefixlen(),
                          global_ril_config->ril_broadcast());
  }

  gDataCalls[gNextDataCallId] = call;
  gNextDataCallId++;

  ALOGV("Requesting data call setup to APN %s, technology %s, prof %s",
        details[2], details[0], details[1]);

  request_or_send_data_calllist(&t);

  gRilConnected = (gDataCalls.size() > 0);
}

static void request_teardown_data_call(void* data, size_t /*datalen*/,
                                       RIL_Token t) {
  char** data_strs = (char**)data;
  int call_id = atoi(data_strs[0]);
  int reason = atoi(data_strs[1]);

  ALOGV("Tearing down data call %d, reason: %d", call_id, reason);

  gDataCalls.erase(call_id);
  gRilConnected = (gDataCalls.size() > 0);

  if (!gRilConnected) {
    TearDownNetworkInterface();
  }
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static void set_radio_state(RIL_RadioState new_state, RIL_Token t) {
  // From header:
  // Toggle radio on and off (for "airplane" mode)
  // If the radio is is turned off/on the radio modem subsystem
  // is expected return to an initialized state. For instance,
  // any voice and data calls will be terminated and all associated
  // lists emptied.
  gDataCalls.clear();

  gSimStatus = SIM_NOT_READY;
  ALOGV("RIL_RadioState change %d to %d", gRadioPowerState, new_state);
  gRadioPowerState = new_state;

  if (new_state == RADIO_STATE_OFF) {
    TearDownNetworkInterface();
  }

  if (t != NULL) {
    gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  }

  gce_ril_env->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED,
                                     NULL, 0);

  pollSIMState(NULL);
}

// returns 1 if on, 0 if off, and -1 on error
static void request_radio_power(void* data, size_t /*datalen*/, RIL_Token t) {
  int on = ((int*)data)[0];
  set_radio_state(on ? RADIO_STATE_ON : RADIO_STATE_OFF, t);
}

// TODO(ender): this should be a class member. Move where it belongs.
struct CallState {
  RIL_CallState state;  // e.g. RIL_CALL_HOLDING;
  bool isInternational;
  bool isMobileTerminated;
  bool isVoice;
  bool isMultiParty;

  std::string number;
  std::string name;
  std::string dtmf;

  bool canPresentNumber;
  bool canPresentName;

  CallState()
      : state(RIL_CallState(0)),
        isInternational(false),
        isMobileTerminated(true),
        isVoice(true),
        isMultiParty(false),
        canPresentNumber(true),
        canPresentName(true) {}

  CallState(const std::string& number)
      : state(RIL_CALL_INCOMING),
        isInternational(false),
        isMobileTerminated(true),
        isVoice(true),
        isMultiParty(false),
        number(number),
        name(number),
        canPresentNumber(true),
        canPresentName(true) {}

  bool isBackground() { return state == RIL_CALL_HOLDING; }

  bool isActive() { return state == RIL_CALL_ACTIVE; }

  bool isDialing() { return state == RIL_CALL_DIALING; }

  bool isIncoming() { return state == RIL_CALL_INCOMING; }

  bool isWaiting() { return state == RIL_CALL_WAITING; }

  void addDtmfDigit(char c) {
    dtmf.push_back(c);
    ALOGV("Call to %s: DTMF %s", number.c_str(), dtmf.c_str());
  }

  bool makeBackground() {
    if (state == RIL_CALL_ACTIVE) {
      state = RIL_CALL_HOLDING;
      return true;
    }

    return false;
  }

  bool makeActive() {
    if (state == RIL_CALL_INCOMING || state == RIL_CALL_WAITING ||
        state == RIL_CALL_DIALING || state == RIL_CALL_HOLDING) {
      state = RIL_CALL_ACTIVE;
      return true;
    }

    return false;
  }
};

static int gLastActiveCallIndex = 1;
static int gMicrophoneMute = 0;
static std::map<int, CallState> gActiveCalls;

static void request_get_current_calls(void* /*data*/, size_t /*datalen*/,
                                      RIL_Token t) {
  const int countCalls = gActiveCalls.size();

  RIL_Call** pp_calls = (RIL_Call**)alloca(countCalls * sizeof(RIL_Call*));
  RIL_Call* p_calls = (RIL_Call*)alloca(countCalls * sizeof(RIL_Call));

  memset(p_calls, 0, countCalls * sizeof(RIL_Call));

  /* init the pointer array */
  for (int i = 0; i < countCalls; i++) {
    pp_calls[i] = &(p_calls[i]);
  }

  // TODO(ender): This should be built from calls requested via RequestDial.
  for (std::map<int, CallState>::iterator iter = gActiveCalls.begin();
       iter != gActiveCalls.end(); ++iter, ++p_calls) {
    p_calls->state = iter->second.state;
    p_calls->index = iter->first;
    p_calls->toa = iter->second.isInternational ? 145 : 129;
    p_calls->isMpty = iter->second.isMultiParty;
    p_calls->isMT = iter->second.isMobileTerminated;
    p_calls->als = iter->first;
    p_calls->isVoice = iter->second.isVoice;
    p_calls->isVoicePrivacy = 0;
    p_calls->number = strdup(iter->second.number.c_str());
    p_calls->numberPresentation = iter->second.canPresentNumber ? 0 : 1;
    p_calls->name = strdup(iter->second.name.c_str());
    p_calls->namePresentation = iter->second.canPresentName ? 0 : 1;
    p_calls->uusInfo = NULL;

    ALOGV("Call to %s (%s): voice=%d mt=%d type=%d state=%d index=%d",
          p_calls->name, p_calls->number, p_calls->isVoice, p_calls->isMT,
          p_calls->toa, p_calls->state, p_calls->index);
  }

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, pp_calls,
                                 countCalls * sizeof(RIL_Call*));

  ALOGV("Get Current calls: %d calls found.\n", countCalls);
}

static void simulate_pending_calls_answered(void* /*ignore*/) {
  ALOGV("Simulating outgoing call answered.");
  // This also resumes held calls.
  for (std::map<int, CallState>::iterator iter = gActiveCalls.begin();
       iter != gActiveCalls.end(); ++iter) {
    if (iter->second.isDialing()) {
      iter->second.makeActive();
    }
  }

  // Only unsolicited here.
  gce_ril_env->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
                                     NULL, 0);
}

static void request_dial(void* data, size_t /*datalen*/, RIL_Token t) {
  RIL_Dial* p_dial = (RIL_Dial*)data;

  ALOGV("Dialing %s, number presentation is %s.", p_dial->address,
        (p_dial->clir == 0) ? "defined by operator"
                            : (p_dial->clir == 1) ? "allowed" : "restricted");

  CallState state(p_dial->address);
  state.isMobileTerminated = false;
  state.state = RIL_CALL_DIALING;

  switch (p_dial->clir) {
    case 0:  // default
    case 1:  // allow
      state.canPresentNumber = true;
      break;

    case 2:  // restrict
      state.canPresentNumber = false;
      break;
  }

  int call_index = gLastActiveCallIndex++;
  gActiveCalls[call_index] = state;

  static const struct timeval kAnswerTime = {5, 0};
  gce_ril_env->RequestTimedCallback(simulate_pending_calls_answered, NULL,
                                    &kAnswerTime);

  // success or failure is ignored by the upper layer here.
  // it will call GET_CURRENT_CALLS and determine success that way
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

void request_set_mute(void* data, size_t /*datalen*/, RIL_Token t) {
  gMicrophoneMute = ((int*)data)[0] != 0;
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

void request_get_mute(RIL_Token t) {
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &gMicrophoneMute,
                                 sizeof(gMicrophoneMute));
}

// TODO(ender): this should be a class member. Move where it belongs.
struct SmsMessage {
  enum SmsStatus { kUnread = 0, kRead = 1, kUnsent = 2, kSent = 3 };

  std::string message;
  SmsStatus status;
};

static int gNextMessageId = 1;
static std::map<int, SmsMessage> gMessagesOnSimCard;

static void request_write_sms_to_sim(void* data, size_t /*datalen*/,
                                     RIL_Token t) {
  RIL_SMS_WriteArgs* p_args = (RIL_SMS_WriteArgs*)data;

  SmsMessage message;
  message.status = SmsMessage::SmsStatus(p_args->status);
  message.message = p_args->pdu;

  ALOGV("Storing SMS message: '%s' with state: %s.", message.message.c_str(),
        (message.status < SmsMessage::kUnsent)
            ? ((message.status == SmsMessage::kRead) ? "READ" : "UNREAD")
            : ((message.status == SmsMessage::kSent) ? "SENT" : "UNSENT"));

  // TODO(ender): simulate SIM FULL?
  int index = gNextMessageId++;
  gMessagesOnSimCard[index] = message;

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &index, sizeof(index));
}

static void request_delete_sms_on_sim(void* data, size_t /*datalen*/,
                                      RIL_Token t) {
  int index = *(int*)data;

  ALOGV("Delete SMS message %d", index);

  if (gMessagesOnSimCard.erase(index) == 0) {
    // No such message
    gce_ril_env->OnRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    return;
  }

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static void request_hangup(void* data, size_t /*datalen*/, RIL_Token t) {
  int* p_line = (int*)data;

  ALOGV("Hanging up call %d.", *p_line);
  std::map<int, CallState>::iterator iter = gActiveCalls.find(*p_line);

  if (iter == gActiveCalls.end()) {
    ALOGV("No such call: %d.", *p_line);
    gce_ril_env->OnRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
  } else {
    gActiveCalls.erase(iter);
    gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  }
}

static void request_hangup_waiting(void* /*data*/, size_t /*datalen*/,
                                   RIL_Token t) {
  ALOGV("Hanging up background/held calls.");
  for (std::map<int, CallState>::iterator iter = gActiveCalls.begin();
       iter != gActiveCalls.end();) {
    if (iter->second.isBackground()) {
      // C++98 -- std::map::erase doesn't return iterator.
      std::map<int, CallState>::iterator temp = iter++;
      gActiveCalls.erase(temp);
    } else {
      ++iter;
    }
  }
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static void request_hangup_current(RIL_Token t) {
  ALOGV("Hanging up foreground/active calls.");
  // This also resumes held calls.
  for (std::map<int, CallState>::iterator iter = gActiveCalls.begin();
       iter != gActiveCalls.end();) {
    if (iter->second.isBackground()) {
      iter->second.makeActive();
      ++iter;
    } else {
      // C++98 -- std::map::erase doesn't return iterator.
      std::map<int, CallState>::iterator temp = iter++;
      gActiveCalls.erase(temp);
    }
  }
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static void request_switch_current_and_waiting(RIL_Token t) {
  ALOGV("Toggle foreground and background calls.");
  // TODO(ender): fix all states. Max 2 calls.
  //   BEFORE                               AFTER
  // Call 1   Call 2                 Call 1       Call 2
  // ACTIVE   HOLDING                HOLDING     ACTIVE
  // ACTIVE   WAITING                HOLDING     ACTIVE
  // HOLDING  WAITING                HOLDING     ACTIVE
  // ACTIVE   IDLE                   HOLDING     IDLE
  // IDLE     IDLE                   IDLE        IDLE
  for (std::map<int, CallState>::iterator iter = gActiveCalls.begin();
       iter != gActiveCalls.end(); ++iter) {
    // TODO(ender): call could also be waiting or dialing or...
    if (iter->second.isBackground()) {
      iter->second.makeActive();
    } else {
      iter->second.makeBackground();
    }
  }
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static void request_answer_incoming(RIL_Token t) {
  ALOGV("Answering incoming call.");

  // There's two types of incoming calls:
  // - incoming: we are receiving this call while nothing happens,
  // - waiting: we are receiving this call while we're already talking.
  // We only accept the incoming ones.
  for (std::map<int, CallState>::iterator iter = gActiveCalls.begin();
       iter != gActiveCalls.end(); ++iter) {
    if (iter->second.isIncoming()) {
      iter->second.makeActive();
    }
  }
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static void request_combine_multiparty_call(void* /*data*/, size_t /*datalen*/,
                                            RIL_Token t) {
  ALOGW("Combine a held call to conversation.");
  for (std::map<int, CallState>::iterator iter = gActiveCalls.begin();
       iter != gActiveCalls.end(); ++iter) {
    if (!iter->second.isVoice) {
      continue;
    }
    if (iter->second.isBackground()) {
      iter->second.makeActive();
      break;
    }
  }
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static void request_split_multiparty_call(void* data, size_t /*datalen*/,
                                          RIL_Token t) {
  int index = *(int*)data;
  ALOGW("Hold all active call except given call: %d", index);
  for (std::map<int, CallState>::iterator iter = gActiveCalls.begin();
    iter != gActiveCalls.end(); ++iter) {
    if (!iter->second.isVoice) {
      continue;
    }
    if (iter->second.isActive() && index != iter->first) {
      iter->second.makeBackground();
      break;
    }
  }
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static void request_udub_on_incoming_calls(RIL_Token t) {
  // UDUB = user determined user busy.
  // We don't exactly do that. We simply drop these calls.
  ALOGV("Reporting busy signal to incoming calls.");
  for (std::map<int, CallState>::iterator iter = gActiveCalls.begin();
       iter != gActiveCalls.end();) {
    // If we have an incoming call, there should be no waiting call.
    // If we have a waiting call, then previous incoming call has been answered.
    if (iter->second.isIncoming() || iter->second.isWaiting()) {
      // C++98 -- std::map::erase doesn't return iterator.
      std::map<int, CallState>::iterator temp = iter++;
      gActiveCalls.erase(temp);
    } else {
      ++iter;
    }
  }
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static void request_send_dtmf(void* data, size_t /*datalen*/, RIL_Token t) {
  char c = ((char*)data)[0];
  ALOGV("Sending DTMF digit '%c'", c);

  for (std::map<int, CallState>::iterator iter = gActiveCalls.begin();
       iter != gActiveCalls.end(); ++iter) {
    if (iter->second.isActive()) {
      iter->second.addDtmfDigit(c);
      break;
    }
  }

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static void request_send_dtmf_stop(RIL_Token t) {
  ALOGV("DTMF tone end.");

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

// Check SignalStrength.java file for more details on how these map to signal
// strength bars.
const int kGatewaySignalStrengthMin = 4;
const int kGatewaySignalStrengthMax = 30;
const int kCDMASignalStrengthMin = -110;
const int kCDMASignalStrengthMax = -60;
const int kEVDOSignalStrengthMin = -160;
const int kEVDOSignalStrengthMax = -70;
const int kLTESignalStrengthMin = 4;
const int kLTESignalStrengthMax = 30;

static int gGatewaySignalStrength = kGatewaySignalStrengthMax;
static int gCDMASignalStrength = kCDMASignalStrengthMax;
static int gEVDOSignalStrength = kEVDOSignalStrengthMax;
static int gLTESignalStrength = kLTESignalStrengthMax;

static void request_signal_strength(void* /*data*/, size_t /*datalen*/,
                                    RIL_Token t) {
  // TODO(ender): possible to support newer APIs here.
  RIL_SignalStrength_v10 strength;

  gGatewaySignalStrength += (rand() % 3 - 1);
  gCDMASignalStrength += (rand() % 3 - 1);
  gEVDOSignalStrength += (rand() % 3 - 1);
  gLTESignalStrength += (rand() % 3 - 1);

  if (gGatewaySignalStrength < kGatewaySignalStrengthMin)
    gGatewaySignalStrength = kGatewaySignalStrengthMin;
  if (gGatewaySignalStrength > kGatewaySignalStrengthMax)
    gGatewaySignalStrength = kGatewaySignalStrengthMax;
  if (gCDMASignalStrength < kCDMASignalStrengthMin)
    gCDMASignalStrength = kCDMASignalStrengthMin;
  if (gCDMASignalStrength > kCDMASignalStrengthMax)
    gCDMASignalStrength = kCDMASignalStrengthMax;
  if (gEVDOSignalStrength < kEVDOSignalStrengthMin)
    gEVDOSignalStrength = kEVDOSignalStrengthMin;
  if (gEVDOSignalStrength > kEVDOSignalStrengthMax)
    gEVDOSignalStrength = kEVDOSignalStrengthMax;
  if (gLTESignalStrength < kLTESignalStrengthMin)
    gLTESignalStrength = kLTESignalStrengthMin;
  if (gLTESignalStrength > kLTESignalStrengthMax)
    gLTESignalStrength = kLTESignalStrengthMax;

  strength.GW_SignalStrength.signalStrength = gGatewaySignalStrength;
  strength.GW_SignalStrength.bitErrorRate = 0;  // 0..7%

  strength.CDMA_SignalStrength.dbm = gCDMASignalStrength;
  strength.CDMA_SignalStrength.ecio = 0;  // Ec/Io; keep high to use dbm.

  strength.EVDO_SignalStrength.dbm = gEVDOSignalStrength;
  strength.EVDO_SignalStrength.ecio = 0;  // Ec/Io; keep high to use dbm.

  strength.LTE_SignalStrength.signalStrength = gLTESignalStrength;
  strength.LTE_SignalStrength.rsrp = INT_MAX;   // Invalid = Use signalStrength.
  strength.LTE_SignalStrength.rsrq = INT_MAX;   // Invalid = Use signalStrength.
  strength.LTE_SignalStrength.rssnr = INT_MAX;  // Invalid = Use signalStrength.
  strength.LTE_SignalStrength.cqi = INT_MAX;    // Invalid = Use signalStrength.

  ALOGV("Reporting signal strength: GW=%d CDMA=%d EVDO=%d LTE=%d",
        gGatewaySignalStrength, gCDMASignalStrength, gEVDOSignalStrength,
        gLTESignalStrength);

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &strength, sizeof(strength));
}

static std::map<RIL_PreferredNetworkType, int> gModemSupportedNetworkTypes;

static void init_modem_supported_network_types() {
  gModemSupportedNetworkTypes[PREF_NET_TYPE_GSM_WCDMA] = MDM_GSM | MDM_WCDMA;
  gModemSupportedNetworkTypes[PREF_NET_TYPE_GSM_ONLY] = MDM_GSM;
  gModemSupportedNetworkTypes[PREF_NET_TYPE_WCDMA] = MDM_WCDMA;
  gModemSupportedNetworkTypes[PREF_NET_TYPE_GSM_WCDMA_AUTO] =
      MDM_GSM | MDM_WCDMA;
  gModemSupportedNetworkTypes[PREF_NET_TYPE_CDMA_EVDO_AUTO] =
      MDM_CDMA | MDM_EVDO;
  gModemSupportedNetworkTypes[PREF_NET_TYPE_CDMA_ONLY] = MDM_CDMA;
  gModemSupportedNetworkTypes[PREF_NET_TYPE_EVDO_ONLY] = MDM_EVDO;
  gModemSupportedNetworkTypes[PREF_NET_TYPE_GSM_WCDMA_CDMA_EVDO_AUTO] =
      MDM_GSM | MDM_WCDMA | MDM_CDMA | MDM_EVDO;
  gModemSupportedNetworkTypes[PREF_NET_TYPE_LTE_CDMA_EVDO] =
      MDM_LTE | MDM_CDMA | MDM_EVDO;
  gModemSupportedNetworkTypes[PREF_NET_TYPE_LTE_GSM_WCDMA] =
      MDM_LTE | MDM_GSM | MDM_WCDMA;
  gModemSupportedNetworkTypes[PREF_NET_TYPE_LTE_CMDA_EVDO_GSM_WCDMA] =
      MDM_LTE | MDM_CDMA | MDM_EVDO | MDM_GSM | MDM_WCDMA;
  gModemSupportedNetworkTypes[PREF_NET_TYPE_LTE_ONLY] = MDM_LTE;
}

static std::map<RIL_PreferredNetworkType, int> gModemTechnologies;

RIL_RadioTechnology gDataTechnologiesPreferenceOrder[] = {
    RADIO_TECH_LTE,    RADIO_TECH_EHRPD, RADIO_TECH_HSPAP,  RADIO_TECH_HSPA,
    RADIO_TECH_HSDPA,  RADIO_TECH_HSUPA, RADIO_TECH_EVDO_B, RADIO_TECH_EVDO_A,
    RADIO_TECH_EVDO_0, RADIO_TECH_1xRTT, RADIO_TECH_UMTS,   RADIO_TECH_EDGE,
    RADIO_TECH_GPRS};

RIL_RadioTechnology gVoiceTechnologiesPreferenceOrder[] = {
    RADIO_TECH_LTE,    RADIO_TECH_EHRPD, RADIO_TECH_EVDO_B, RADIO_TECH_EVDO_A,
    RADIO_TECH_EVDO_0, RADIO_TECH_1xRTT, RADIO_TECH_IS95B,  RADIO_TECH_IS95A,
    RADIO_TECH_UMTS,   RADIO_TECH_GSM};

static void init_modem_technologies() {
  gModemTechnologies[PREF_NET_TYPE_GSM_WCDMA] =
      (1 << RADIO_TECH_GSM) | (1 << RADIO_TECH_GPRS) | (1 << RADIO_TECH_EDGE) |
      (1 << RADIO_TECH_UMTS);
  gModemTechnologies[PREF_NET_TYPE_GSM_ONLY] =
      (1 << RADIO_TECH_GSM) | (1 << RADIO_TECH_GPRS) | (1 << RADIO_TECH_EDGE);
  gModemTechnologies[PREF_NET_TYPE_WCDMA] =
      (1 << RADIO_TECH_EDGE) | (1 << RADIO_TECH_UMTS);
  gModemTechnologies[PREF_NET_TYPE_GSM_WCDMA_AUTO] =
      (1 << RADIO_TECH_GSM) | (1 << RADIO_TECH_GPRS) | (1 << RADIO_TECH_EDGE) |
      (1 << RADIO_TECH_UMTS);
  gModemTechnologies[PREF_NET_TYPE_CDMA_EVDO_AUTO] =
      (1 << RADIO_TECH_IS95A) | (1 << RADIO_TECH_IS95B) |
      (1 << RADIO_TECH_1xRTT) | (1 << RADIO_TECH_EVDO_0) |
      (1 << RADIO_TECH_EVDO_A) | (1 << RADIO_TECH_HSDPA) |
      (1 << RADIO_TECH_HSUPA) | (1 << RADIO_TECH_HSPA) |
      (1 << RADIO_TECH_EVDO_B);
  gModemTechnologies[PREF_NET_TYPE_CDMA_ONLY] = (1 << RADIO_TECH_IS95A) |
                                                (1 << RADIO_TECH_IS95B) |
                                                (1 << RADIO_TECH_1xRTT);
  gModemTechnologies[PREF_NET_TYPE_EVDO_ONLY] =
      (1 << RADIO_TECH_EVDO_0) | (1 << RADIO_TECH_EVDO_A) |
      (1 << RADIO_TECH_EVDO_A) | (1 << RADIO_TECH_HSDPA) |
      (1 << RADIO_TECH_HSUPA) | (1 << RADIO_TECH_HSPA) |
      (1 << RADIO_TECH_EVDO_B);
  gModemTechnologies[PREF_NET_TYPE_GSM_WCDMA_CDMA_EVDO_AUTO] =
      (1 << RADIO_TECH_GSM) | (1 << RADIO_TECH_GPRS) | (1 << RADIO_TECH_EDGE) |
      (1 << RADIO_TECH_UMTS) | (1 << RADIO_TECH_IS95A) |
      (1 << RADIO_TECH_IS95B) | (1 << RADIO_TECH_1xRTT) |
      (1 << RADIO_TECH_EVDO_0) | (1 << RADIO_TECH_EVDO_A) |
      (1 << RADIO_TECH_HSDPA) | (1 << RADIO_TECH_HSUPA) |
      (1 << RADIO_TECH_HSPA) | (1 << RADIO_TECH_EVDO_B);
  gModemTechnologies[PREF_NET_TYPE_LTE_CDMA_EVDO] =
      (1 << RADIO_TECH_HSPAP) | (1 << RADIO_TECH_LTE) |
      (1 << RADIO_TECH_EHRPD) | (1 << RADIO_TECH_IS95A) |
      (1 << RADIO_TECH_IS95B) | (1 << RADIO_TECH_1xRTT) |
      (1 << RADIO_TECH_EVDO_0) | (1 << RADIO_TECH_EVDO_A) |
      (1 << RADIO_TECH_HSDPA) | (1 << RADIO_TECH_HSUPA) |
      (1 << RADIO_TECH_HSPA) | (1 << RADIO_TECH_EVDO_B);
  gModemTechnologies[PREF_NET_TYPE_LTE_GSM_WCDMA] =
      (1 << RADIO_TECH_HSPAP) | (1 << RADIO_TECH_LTE) |
      (1 << RADIO_TECH_EHRPD) | (1 << RADIO_TECH_GSM) | (1 << RADIO_TECH_GPRS) |
      (1 << RADIO_TECH_EDGE) | (1 << RADIO_TECH_UMTS);

  gModemTechnologies[PREF_NET_TYPE_LTE_CMDA_EVDO_GSM_WCDMA] =
      (1 << RADIO_TECH_HSPAP) | (1 << RADIO_TECH_LTE) |
      (1 << RADIO_TECH_EHRPD) | (1 << RADIO_TECH_IS95A) |
      (1 << RADIO_TECH_IS95B) | (1 << RADIO_TECH_1xRTT) |
      (1 << RADIO_TECH_EVDO_0) | (1 << RADIO_TECH_EVDO_A) |
      (1 << RADIO_TECH_HSDPA) | (1 << RADIO_TECH_HSUPA) |
      (1 << RADIO_TECH_HSPA) | (1 << RADIO_TECH_EVDO_B) |
      (1 << RADIO_TECH_GSM) | (1 << RADIO_TECH_GPRS) | (1 << RADIO_TECH_EDGE) |
      (1 << RADIO_TECH_UMTS);
  gModemTechnologies[PREF_NET_TYPE_LTE_ONLY] =
      (1 << RADIO_TECH_HSPAP) | (1 << RADIO_TECH_LTE) | (1 << RADIO_TECH_EHRPD);
}

static const RIL_PreferredNetworkType gModemDefaultType =
    PREF_NET_TYPE_LTE_GSM_WCDMA;
static RIL_PreferredNetworkType gModemCurrentType = gModemDefaultType;
static RIL_RadioTechnology gModemVoiceTechnology = RADIO_TECH_LTE;

// Report technology change.
// Select best technology from the list of supported techs.
// Demotes RADIO_TECH_GSM as it's voice-only.
static RIL_RadioTechnology getBestDataTechnology(
    RIL_PreferredNetworkType network_type) {
  RIL_RadioTechnology technology = RADIO_TECH_GPRS;

  std::map<RIL_PreferredNetworkType, int>::iterator iter =
      gModemTechnologies.find(network_type);

  ALOGV("Searching for best data technology for network type %d...",
        network_type);

  // Find which technology bits are lit. Pick the top most.
  for (size_t tech_index = 0;
       tech_index < sizeof(gDataTechnologiesPreferenceOrder) /
                        sizeof(gDataTechnologiesPreferenceOrder[0]);
       ++tech_index) {
    if (iter->second & (1 << gDataTechnologiesPreferenceOrder[tech_index])) {
      technology = gDataTechnologiesPreferenceOrder[tech_index];
      break;
    }
  }

  ALOGV("Best data technology: %d.", technology);
  return technology;
}

static RIL_RadioTechnology getBestVoiceTechnology(
    RIL_PreferredNetworkType network_type) {
  RIL_RadioTechnology technology = RADIO_TECH_GSM;

  std::map<RIL_PreferredNetworkType, int>::iterator iter =
      gModemTechnologies.find(network_type);

  ALOGV("Searching for best voice technology for network type %d...",
        network_type);

  // Find which technology bits are lit. Pick the top most.
  for (size_t tech_index = 0;
       tech_index < sizeof(gVoiceTechnologiesPreferenceOrder) /
                        sizeof(gVoiceTechnologiesPreferenceOrder[0]);
       ++tech_index) {
    if (iter->second & (1 << gVoiceTechnologiesPreferenceOrder[tech_index])) {
      technology = gVoiceTechnologiesPreferenceOrder[tech_index];
      break;
    }
  }

  ALOGV("Best voice technology: %d.", technology);
  return technology;
}

static void setRadioTechnology(RIL_PreferredNetworkType network_type) {
  RIL_RadioTechnology technology = getBestVoiceTechnology(network_type);

  if (technology != gModemVoiceTechnology) {
    gModemVoiceTechnology = technology;
    gce_ril_env->OnUnsolicitedResponse(RIL_UNSOL_VOICE_RADIO_TECH_CHANGED,
                                       &gModemVoiceTechnology,
                                       sizeof(gModemVoiceTechnology));
  }
}

static void request_get_radio_capability(RIL_Token t) {
  ALOGV("Requesting radio capability.");
  RIL_RadioCapability rc;
  rc.version = RIL_RADIO_CAPABILITY_VERSION;
  rc.session = 1;
  rc.phase = RC_PHASE_CONFIGURED;
  rc.rat = RAF_HSPAP;
  strncpy(rc.logicalModemUuid, "com.google.cvdgce1.modem", MAX_UUID_LENGTH);
  rc.status = RC_STATUS_SUCCESS;
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &rc, sizeof(rc));
}

static void request_set_radio_capability(void* data, size_t datalen,
                                         RIL_Token t) {
  RIL_RadioCapability* rc = (RIL_RadioCapability*)data;
  ALOGV(
      "RadioCapability version %d session %d phase %d rat %d "
      "logicalModemUuid %s status %d",
      rc->version, rc->session, rc->phase, rc->rat, rc->logicalModemUuid,
      rc->status);
  // TODO(ender): do something about these numbers.
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, rc, datalen);
}

static void request_set_preferred_network_type(int /*request*/, void* data,
                                               size_t /*datalen*/,
                                               RIL_Token t) {
  RIL_PreferredNetworkType desired_type = *(RIL_PreferredNetworkType*)(data);

  // TODO(ender): telephony still believes this phone is GSM only.
  ALOGV("Requesting modem technology change -> %d", desired_type);

  if (gModemSupportedNetworkTypes.find(desired_type) ==
      gModemSupportedNetworkTypes.end()) {
    desired_type = gModemSupportedNetworkTypes.begin()->first;
  }

  if (gModemCurrentType == desired_type) {
    ALOGV("Modem technology already set to %d.", desired_type);
    gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
    return;
  }

  int supported_technologies = gModemSupportedNetworkTypes[gModemDefaultType];
  int desired_technologies = gModemSupportedNetworkTypes[desired_type];

  ALOGV("Requesting modem technology change %d -> %d", gModemCurrentType,
        desired_type);

  // Check if we support this technology.
  if ((supported_technologies & desired_technologies) != desired_technologies) {
    ALOGV("Desired technology is not supported.");
    gce_ril_env->OnRequestComplete(t, RIL_E_MODE_NOT_SUPPORTED, NULL, 0);
    return;
  }

  gModemCurrentType = desired_type;
  setRadioTechnology(desired_type);
  ALOGV("Technology change successful.");
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static void request_get_preferred_network_type(int /*request*/, void* /*data*/,
                                               size_t /*datalen*/,
                                               RIL_Token t) {
  gce_ril_env->OnRequestComplete(
      t, RIL_E_SUCCESS,
      const_cast<RIL_PreferredNetworkType*>(&gModemDefaultType),
      sizeof(gModemDefaultType));
}

enum RegistrationState {
  kUnregistered = 0,
  kRegisteredInHomeNetwork = 1,
  kSearchingForOperators = 2,
  kRegistrationDenied = 3,
  kUnknown = 4,
  kRegisteredInRoamingMode = 5,

  kUnregistered_EmergencyCallsOnly = 10,
  kSearchingForOperators_EmergencyCallsOnly = 12,
  kRegistrationDenied_EmergencyCallsOnly = 13,
  kUnknown_EmergencyCallsOnly = 14
};

static const char kCdmaMobileDeviceNumber[] = "5551234567";
static const char kCdmaSID[] = "123";
static const char kCdmaNID[] = "65535";  // special: indicates free roaming.

static void request_registration_state(int request, void* /*data*/,
                                       size_t /*datalen*/, RIL_Token t) {
  char** responseStr = NULL;
  int numElements = 0;

  // See RIL_REQUEST_VOICE_REGISTRATION_STATE and
  // RIL_REQUEST_DATA_REGISTRATION_STATE.
  numElements = (request == RIL_REQUEST_VOICE_REGISTRATION_STATE) ? 15 : 6;
  responseStr = (char**)malloc(numElements * sizeof(char*));

  asprintf(&responseStr[0], "%d", kRegisteredInHomeNetwork);
  responseStr[1] = NULL;  // LAC - needed for GSM / WCDMA only.
  responseStr[2] = NULL;  // CID - needed for GSM / WCDMA only.

  // This is (and always has been) a huge memory leak.
  if (request == RIL_REQUEST_VOICE_REGISTRATION_STATE) {
    ALOGV("Requesting voice registration state.");
    asprintf(&responseStr[3], "%d", getBestVoiceTechnology(gModemCurrentType));
    responseStr[4] = strdup("1");       // BSID
    responseStr[5] = strdup("123");     // Latitude
    responseStr[6] = strdup("222");     // Longitude
    responseStr[7] = strdup("0");       // CSS Indicator
    responseStr[8] = strdup(kCdmaSID);  // SID
    responseStr[9] = strdup(kCdmaNID);  // NID
    responseStr[10] = strdup("0");      // Roaming indicator
    responseStr[11] = strdup("1");      // System is in PRL
    responseStr[12] = strdup("0");      // Default Roaming indicator
    responseStr[13] = strdup("0");      // Reason for denial
    responseStr[14] = strdup("0");      // Primary Scrambling Code of Current
  } else if (request == RIL_REQUEST_DATA_REGISTRATION_STATE) {
    ALOGV("Requesting data registration state.");
    asprintf(&responseStr[3], "%d", getBestDataTechnology(gModemCurrentType));
    responseStr[4] = strdup("");   // DataServiceDenyReason
    responseStr[5] = strdup("1");  // Max simultaneous data calls.
  } else {
    ALOGV("Unexpected request type: %d", request);
    return;
  }

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, responseStr,
                                 numElements * sizeof(responseStr));
}

static void request_baseband_version(RIL_Token t) {
  const char* response_str = "CVD_R1.0.0";

  ALOGV("Requested phone baseband version.");

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, strdup(response_str),
                                 sizeof(response_str));
}

// Returns true, if modem is CDMA capable.
static bool isCDMA() {
  switch (gModemCurrentType) {
    case PREF_NET_TYPE_GSM_WCDMA:
    case PREF_NET_TYPE_GSM_ONLY:
    case PREF_NET_TYPE_WCDMA:
    case PREF_NET_TYPE_GSM_WCDMA_AUTO:
    case PREF_NET_TYPE_LTE_GSM_WCDMA:
    case PREF_NET_TYPE_LTE_ONLY:
      return false;

    case PREF_NET_TYPE_CDMA_EVDO_AUTO:
    case PREF_NET_TYPE_CDMA_ONLY:
    case PREF_NET_TYPE_EVDO_ONLY:
    case PREF_NET_TYPE_LTE_CDMA_EVDO:
    case PREF_NET_TYPE_LTE_CMDA_EVDO_GSM_WCDMA:
    case PREF_NET_TYPE_GSM_WCDMA_CDMA_EVDO_AUTO:
      return true;
    default:
      break;
  }

  ALOGE("INVALID MODEM TYPE: %d", gModemCurrentType);
  return false;
}

// Returns true, if modem is GSM capable.
// Note, this is not same as !isCDMA().
static bool isGSM() {
  switch (gModemCurrentType) {
    case PREF_NET_TYPE_GSM_WCDMA:
    case PREF_NET_TYPE_GSM_ONLY:
    case PREF_NET_TYPE_WCDMA:
    case PREF_NET_TYPE_GSM_WCDMA_AUTO:
    case PREF_NET_TYPE_LTE_GSM_WCDMA:
    case PREF_NET_TYPE_LTE_ONLY:
    case PREF_NET_TYPE_GSM_WCDMA_CDMA_EVDO_AUTO:
      return true;

    case PREF_NET_TYPE_CDMA_EVDO_AUTO:
    case PREF_NET_TYPE_CDMA_ONLY:
    case PREF_NET_TYPE_EVDO_ONLY:
    case PREF_NET_TYPE_LTE_CDMA_EVDO:
    case PREF_NET_TYPE_LTE_CMDA_EVDO_GSM_WCDMA:
      return false;
    default:
      break;
  }

  ALOGE("INVALID MODEM TYPE: %d", gModemCurrentType);
  return false;
}

static const char gIdentityGsmImei[] = "12345678902468";  // Luhn cksum = 0.
static const char gIdentityGsmImeiSv[] = "01";            // Arbitrary version.
static const char gIdentityCdmaEsn[] = "A0123456";        // 8 digits, ^[A-F].*
static const char gIdentityCdmaMeid[] =
    "A0123456789012";  // 14 digits, ^[A-F].*

static void request_get_imei(RIL_Token t) {
  ALOGV("Requesting IMEI");
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS,
                                 const_cast<char*>(gIdentityGsmImei),
                                 strlen(gIdentityGsmImei));
}

static void request_get_imei_sv(RIL_Token t) {
  ALOGV("Requesting IMEI SV");
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS,
                                 const_cast<char*>(gIdentityGsmImeiSv),
                                 strlen(gIdentityGsmImeiSv));
}

static void request_device_identity(int /*request*/, void* /*data*/,
                                    size_t /*datalen*/, RIL_Token t) {
  char* response[4] = {NULL};

  ALOGV("Requesting device identity...");

  if (isCDMA()) {
    response[2] = strdup(&gIdentityCdmaEsn[0]);
    response[3] = strdup(&gIdentityCdmaMeid[0]);
  }

  if (isGSM()) {
    response[0] = strdup(&gIdentityGsmImei[0]);
    response[1] = strdup(&gIdentityGsmImeiSv[0]);
  }

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(response));

  free(response[0]);
  free(response[1]);
}

// Let's pretend we have SIM for CDMA (by default).
static bool gCdmaHasSim = true;
static RIL_CdmaSubscriptionSource gCdmaSubscriptionType =
    CDMA_SUBSCRIPTION_SOURCE_RUIM_SIM;

static void request_cdma_get_subscription_source(int /*request*/,
                                                 void* /*data*/,
                                                 size_t /*datalen*/,
                                                 RIL_Token t) {
  ALOGV("Requesting CDMA Subscription source.");

  if (!isCDMA()) {
    // No such radio.
    gce_ril_env->OnRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
    return;
  }

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &gCdmaSubscriptionType,
                                 sizeof(gCdmaSubscriptionType));
}

static void request_cdma_set_subscription_source(int /*request*/, void* data,
                                                 size_t /*datalen*/,
                                                 RIL_Token t) {
  ALOGV("Setting CDMA Subscription source.");

  if (!isCDMA()) {
    // No such radio.
    gce_ril_env->OnRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
    return;
  }

  RIL_CdmaSubscriptionSource new_source = *(RIL_CdmaSubscriptionSource*)(data);

  if (new_source == CDMA_SUBSCRIPTION_SOURCE_RUIM_SIM && !gCdmaHasSim) {
    // No such radio.
    gce_ril_env->OnRequestComplete(t, RIL_E_SIM_ABSENT, NULL, 0);
    return;
  }

  ALOGV("Changed CDMA subscription type from %d to %d", gCdmaSubscriptionType,
        new_source);
  gCdmaSubscriptionType = new_source;

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  gce_ril_env->OnUnsolicitedResponse(RIL_UNSOL_CDMA_SUBSCRIPTION_SOURCE_CHANGED,
                                     &gCdmaSubscriptionType,
                                     sizeof(gCdmaSubscriptionType));
}

static void request_cdma_subscription(int /*request*/, void* /*data*/,
                                      size_t /*datalen*/, RIL_Token t) {
  ALOGV("Requesting CDMA Subscription.");

  if (!isCDMA()) {
    // No such radio.
    gce_ril_env->OnRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
    return;
  }

  char* responseStr[5] = {NULL};
  responseStr[0] = strdup(&kCdmaMobileDeviceNumber[0]);  // MDN
  responseStr[1] = strdup(&kCdmaSID[0]);                 // SID
  responseStr[2] = strdup(&kCdmaNID[0]);                 // NID
  responseStr[3] = strdup(&kCdmaMobileDeviceNumber[0]);  // MIN
  responseStr[4] = strdup("1");                          // PRL Version
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, responseStr,
                                 sizeof(responseStr));
}

static const int gMaxConcurrentVoiceCalls = 4;
static const int gMaxConcurrentDataCalls = 4;
static const int gMaxConcurrentStandbyConnections = 4;

static void request_hardware_config(RIL_Token t) {
  RIL_HardwareConfig hw_cfg[2];

  ALOGV("Requesting hardware configuration.");

  strncpy(hw_cfg[0].uuid, "com.google.cvdgce1.modem", sizeof(hw_cfg[0].uuid));
  strncpy(hw_cfg[1].uuid, "com.google.cvdgce1.sim", sizeof(hw_cfg[1].uuid));

  int technologies = 0;  // = unknown.
  std::map<RIL_PreferredNetworkType, int>::iterator iter =
      gModemTechnologies.find(gModemDefaultType);
  if (iter != gModemTechnologies.end()) {
    technologies = iter->second;
  }

  hw_cfg[0].type = RIL_HARDWARE_CONFIG_MODEM;
  hw_cfg[0].state = RIL_HARDWARE_CONFIG_STATE_ENABLED;
  hw_cfg[0].cfg.modem.rilModel = 0;
  hw_cfg[0].cfg.modem.rat = technologies;
  hw_cfg[0].cfg.modem.maxVoice = gMaxConcurrentVoiceCalls;
  hw_cfg[0].cfg.modem.maxData = gMaxConcurrentDataCalls;
  hw_cfg[0].cfg.modem.maxStandby = gMaxConcurrentStandbyConnections;

  hw_cfg[1].type = RIL_HARDWARE_CONFIG_SIM;
  hw_cfg[1].state = RIL_HARDWARE_CONFIG_STATE_ENABLED;
  memcpy(hw_cfg[1].cfg.sim.modemUuid, hw_cfg[0].uuid,
         sizeof(hw_cfg[1].cfg.sim.modemUuid));

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &hw_cfg, sizeof(hw_cfg));
}

// 0 = Home network only, 1 = preferred networks only, 2 = all networks.
static int gCdmaRoamingPreference = 2;

static void request_cdma_get_roaming_preference(int /*request*/, void* /*data*/,
                                                size_t /*datalen*/,
                                                RIL_Token t) {
  if (!isCDMA()) {
    // No such radio.
    gce_ril_env->OnRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
    return;
  }

  ALOGV("Requesting CDMA Roaming preference");

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &gCdmaRoamingPreference,
                                 sizeof(gCdmaRoamingPreference));
}

static void request_cdma_set_roaming_preference(int /*request*/, void* data,
                                                size_t /*datalen*/,
                                                RIL_Token t) {
  if (!isCDMA()) {
    // No such radio.
    gce_ril_env->OnRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
    return;
  }

  int pref = *(int*)data;
  ALOGV("Changing CDMA roaming preference: %d -> %d", gCdmaRoamingPreference,
        pref);

  if ((pref < 0) || (pref > 2)) {
    ALOGV("Unsupported roaming preference: %d", pref);
    gce_ril_env->OnRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    return;
  }

  gCdmaRoamingPreference = pref;
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static void request_send_ussd(void* /*data*/, size_t /*datalen*/, RIL_Token t) {
  ALOGV("Sending USSD code is currently not supported");
  // TODO(ender): support this feature
  gce_ril_env->OnRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
}

static void request_cancel_ussd(RIL_Token t) {
  ALOGV("Cancelling USSD code is currently not supported");
  // TODO(ender): support this feature
  gce_ril_env->OnRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
}

static void request_exit_emergency_mode(void* /*data*/, size_t /*datalen*/,
                                        RIL_Token t) {
  ALOGV("Exiting emergency callback mode.");

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static RIL_RadioState gce_ril_current_state() {
  ALOGV("Reporting radio state %d", gRadioPowerState);
  return gRadioPowerState;
}

static int gce_ril_on_supports(int requestCode) {
  ALOGE("%s: Request code %d not implemented", __FUNCTION__, requestCode);
  return 1;
}

static void gce_ril_on_cancel(RIL_Token /*t*/) {
  ALOGE("Cancel operation not implemented");
}

static const char* gce_ril_get_version(void) {
  ALOGV("Reporting Cuttlefish version " CUTTLEFISH_RIL_VERSION_STRING);
  return CUTTLEFISH_RIL_VERSION_STRING;
}

static int s_cell_info_rate_ms = INT_MAX;
static int s_mcc = 0;
static int s_mnc = 0;
static int s_lac = 0;
static int s_cid = 0;

std::vector<RIL_NeighboringCell> gGSMNeighboringCells;

static void request_get_neighboring_cell_ids(void* /*data*/, size_t /*datalen*/,
                                             RIL_Token t) {
  ALOGV("Requesting GSM neighboring cell ids");

  if (!isGSM() || gGSMNeighboringCells.empty()) {
    gce_ril_env->OnRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    return;
  }

  RIL_NeighboringCell** cells =
      new RIL_NeighboringCell*[gGSMNeighboringCells.size()];

  for (size_t index = 0; index < gGSMNeighboringCells.size(); ++index) {
    cells[index] = &gGSMNeighboringCells[index];
  }

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, cells,
                                 sizeof(RIL_NeighboringCell*));
  delete[] cells;
}

static void request_get_cell_info_list(void* /*data*/, size_t /*datalen*/,
                                       RIL_Token t) {
  struct timespec now;
  uint64_t curTime;

  ALOGV("Requesting Cell Info List");

  clock_gettime(CLOCK_MONOTONIC, &now);
  curTime = now.tv_sec * 1000000000LL + now.tv_nsec;

  RIL_CellInfo_v12 ci;

  if (isGSM()) {
    ci.cellInfoType = RIL_CELL_INFO_TYPE_GSM;
    ci.registered = 1;
    ci.timeStampType = RIL_TIMESTAMP_TYPE_ANTENNA;  // Our own timestamp.
    ci.timeStamp = curTime - 1000;                  // Fake time in the past.
    ci.CellInfo.gsm.cellIdentityGsm.mcc = s_mcc;
    ci.CellInfo.gsm.cellIdentityGsm.mnc = s_mnc;
    ci.CellInfo.gsm.cellIdentityGsm.lac = s_lac;
    ci.CellInfo.gsm.cellIdentityGsm.cid = s_cid;
    ci.CellInfo.gsm.signalStrengthGsm.signalStrength = 10;
    ci.CellInfo.gsm.signalStrengthGsm.bitErrorRate = 0;

    gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &ci, sizeof(ci));
  } else if (isCDMA()) {
    // TODO(ender): CDMA cell support. And LTE.
    gce_ril_env->OnRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
  } else {
    gce_ril_env->OnRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
  }
}

struct NetworkOperator {
  std::string long_name;
  std::string short_name;
  bool is_accessible;

  NetworkOperator() {}

  NetworkOperator(const std::string& long_name, const std::string& short_name,
                  bool is_accessible)
      : long_name(long_name),
        short_name(short_name),
        is_accessible(is_accessible) {}
};

static std::map<std::string, NetworkOperator> gNetworkOperators;
static std::string gCurrentNetworkOperator = "";

enum OperatorSelectionMethod {
  kOperatorAutomatic = 0,
  kOperatorManual = 1,
  kOperatorDeregistered = 2,
  kOperatorManualThenAutomatic = 4
};

static void init_virtual_network() {
  gGSMNeighboringCells.resize(1);
  gGSMNeighboringCells[0].cid = (char*)"0000";
  gGSMNeighboringCells[0].rssi = 75;
  gNetworkOperators["311740"] =
      NetworkOperator("Android Virtual Operator", "Android", true);
  gNetworkOperators["310300"] =
      NetworkOperator("Alternative Operator", "Alternative", true);
  gNetworkOperators["310400"] =
      NetworkOperator("Hermetic Network Operator", "Hermetic", false);
}

static OperatorSelectionMethod gOperatorSelectionMethod = kOperatorDeregistered;

static void request_query_network_selection_mode(void* /*data*/,
                                                 size_t /*datalen*/,
                                                 RIL_Token t) {
  ALOGV("Query operator selection mode (%d)", gOperatorSelectionMethod);
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &gOperatorSelectionMethod,
                                 sizeof(gOperatorSelectionMethod));
}

static void request_operator(void* /*data*/, size_t /*datalen*/, RIL_Token t) {
  std::map<std::string, NetworkOperator>::iterator iter =
      gNetworkOperators.find(gCurrentNetworkOperator);

  ALOGV("Requesting current operator info");

  if (iter == gNetworkOperators.end()) {
    gce_ril_env->OnRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
    return;
  }

  const char* response[] = {iter->second.long_name.c_str(),
                            iter->second.short_name.c_str(),
                            iter->first.c_str()};

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, response, sizeof(response));
}

static void request_query_available_networks(void* /*data*/, size_t /*datalen*/,
                                             RIL_Token t) {
  const char** available_networks =
      new const char*[gNetworkOperators.size() * 4];

  ALOGV("Querying available networks.");

  // TODO(ender): this should only respond once operator is selected and
  // registered.
  int index = 0;
  for (std::map<std::string, NetworkOperator>::iterator iter =
           gNetworkOperators.begin();
       iter != gNetworkOperators.end(); ++iter) {
    // TODO(ender): wrap in a neat structure maybe?
    available_networks[index++] = iter->second.long_name.c_str();
    available_networks[index++] = iter->second.short_name.c_str();
    available_networks[index++] = iter->first.c_str();
    if (!iter->second.is_accessible) {
      available_networks[index++] = "forbidden";
    } else if (iter->first == gCurrentNetworkOperator) {
      available_networks[index++] = "current";
    } else {
      available_networks[index++] = "available";
    }
  }

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &available_networks,
                                 4 * gNetworkOperators.size());
  delete[] available_networks;
}

static void request_set_automatic_network_selection(RIL_Token t) {
  ALOGV("Requesting automatic operator selection");
  gCurrentNetworkOperator = gNetworkOperators.begin()->first;
  gOperatorSelectionMethod = kOperatorAutomatic;
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static void request_set_manual_network_selection(void* data, size_t /*datalen*/,
                                                 RIL_Token t) {
  char* mccmnc = (char*)data;

  ALOGV("Requesting manual operator selection: %s", mccmnc);

  std::map<std::string, NetworkOperator>::iterator iter =
      gNetworkOperators.find(mccmnc);

  if (iter == gNetworkOperators.end() || iter->second.is_accessible) {
    gce_ril_env->OnRequestComplete(t, RIL_E_ILLEGAL_SIM_OR_ME, NULL, 0);
    return;
  }

  gCurrentNetworkOperator = mccmnc;
  gOperatorSelectionMethod = kOperatorManual;

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static const char kDefaultSMSC[] = "00";
static int gNextSmsMessageId = 1;

static void request_cdma_send_SMS(void* /*data*/, RIL_Token t) {
  RIL_SMS_Response response = {0, 0, 0};
  // RIL_CDMA_SMS_Message* rcsm = (RIL_CDMA_SMS_Message*) data;

  ALOGW("CDMA SMS Send is currently not implemented.");

  // Cdma Send SMS implementation will go here:
  // But it is not implemented yet.
  memset(&response, 0, sizeof(response));
  response.messageRef = -1;  // This must be BearerData MessageId.
  gce_ril_env->OnRequestComplete(t, RIL_E_SMS_SEND_FAIL_RETRY, &response,
                                 sizeof(response));
}

static void request_send_SMS(void* data, RIL_Token t) {
  RIL_SMS_Response response = {0, 0, 0};

  ALOGV("Send GSM SMS Message");

  // SMSC is an address of SMS center or NULL for default.
  const char* smsc = ((const char**)data)[0];
  if (smsc == NULL) smsc = &kDefaultSMSC[0];

  response.messageRef = gNextSmsMessageId++;
  response.ackPDU = NULL;
  response.errorCode = 0;

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(response));

  // response.messageRef = -1;
  // gce_ril_env->OnRequestComplete(t, RIL_E_SMS_SEND_FAIL_RETRY, &response,
  //                                sizeof(response));
}

static void request_set_cell_info_list_rate(void* data, size_t /*datalen*/,
                                            RIL_Token t) {
  // For now we'll save the rate but no RIL_UNSOL_CELL_INFO_LIST messages
  // will be sent.
  ALOGV("Setting cell info list rate.");
  s_cell_info_rate_ms = ((int*)data)[0];
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
static void request_ims_send_SMS(void* data, size_t /*datalen*/, RIL_Token t) {
  RIL_IMS_SMS_Message* args = (RIL_IMS_SMS_Message*)data;
  RIL_SMS_Response response{};

  ALOGV("Send IMS SMS Message");

  switch (args->tech) {
    case RADIO_TECH_3GPP:
      return request_send_SMS(args->message.gsmMessage, t);

    case RADIO_TECH_3GPP2:
      return request_cdma_send_SMS(args->message.gsmMessage, t);

    default:
      ALOGE("Invalid SMS format value: %d", args->tech);
      response.messageRef = -2;
      gce_ril_env->OnRequestComplete(t, RIL_E_GENERIC_FAILURE, &response,
                                     sizeof(response));
  }
}

static void request_SMS_acknowledge(void* data, size_t /*datalen*/,
                                    RIL_Token t) {
  int* ack = (int*)data;

  // TODO(ender): we should retain "incoming" sms for later reception.
  ALOGV("SMS receipt %ssuccessful (reason %d).", ack[0] ? "" : "un", ack[1]);

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

struct SimFileCommand {
  uint8_t command;
  uint16_t efid;
  uint8_t param1;
  uint8_t param2;
  uint8_t param3;

  bool operator<(const SimFileCommand& other) const {
    uint64_t sum1, sum2;
    sum1 = (command * 1ull << 40) | (efid * 1ull << 24) | (param1 << 16) |
           (param2 << 8) | (param3);
    sum2 = (other.command * 1ull << 40) | (other.efid * 1ull << 24) |
           (other.param1 << 16) | (other.param2 << 8) | (other.param3);
    return sum1 < sum2;
  }

  SimFileCommand(uint8_t cmd, uint16_t efid, uint8_t p1, uint8_t p2, uint8_t p3)
      : command(cmd), efid(efid), param1(p1), param2(p2), param3(p3) {}
};

struct SimFileResponse {
  uint8_t sw1;
  uint8_t sw2;
  const char* data;

  SimFileResponse() : sw1(0), sw2(0), data(NULL) {}

  SimFileResponse(uint8_t sw1, uint8_t sw2, const char* data)
      : sw1(sw1), sw2(sw2), data(data) {}
};

// TODO(ender): Double check & rewrite these.
std::map<SimFileCommand, SimFileResponse> gSimFileSystem;

static void init_sim_file_system() {
  gSimFileSystem[SimFileCommand(192, 28436, 0, 0, 15)] =
      SimFileResponse(144, 0, "000000146f1404001aa0aa01020000");
  gSimFileSystem[SimFileCommand(176, 28436, 0, 0, 20)] =
      SimFileResponse(144, 0, "416e64726f6964ffffffffffffffffffffffffff");
  gSimFileSystem[SimFileCommand(192, 28433, 0, 0, 15)] =
      SimFileResponse(144, 0, "000000016f11040011a0aa01020000");
  gSimFileSystem[SimFileCommand(176, 28433, 0, 0, 1)] =
      SimFileResponse(144, 0, "55");
  gSimFileSystem[SimFileCommand(192, 12258, 0, 0, 15)] =
      SimFileResponse(144, 0, "0000000a2fe204000fa0aa01020000");
  gSimFileSystem[SimFileCommand(176, 12258, 0, 0, 10)] =
      SimFileResponse(144, 0, "98101430121181157002");
  gSimFileSystem[SimFileCommand(192, 28435, 0, 0, 15)] =
      SimFileResponse(144, 0, "000000016f13040011a0aa01020000");
  gSimFileSystem[SimFileCommand(176, 28435, 0, 0, 1)] =
      SimFileResponse(144, 0, "55");
  gSimFileSystem[SimFileCommand(192, 28472, 0, 0, 15)] =
      SimFileResponse(144, 0, "0000000f6f3804001aa0aa01020000");
  gSimFileSystem[SimFileCommand(176, 28472, 0, 0, 15)] =
      SimFileResponse(144, 0, "ff30ffff3c003c03000c0000f03f00");
  gSimFileSystem[SimFileCommand(192, 28617, 0, 0, 15)] =
      SimFileResponse(144, 0, "000000086fc9040011a0aa01020104");
  gSimFileSystem[SimFileCommand(178, 28617, 1, 4, 4)] =
      SimFileResponse(144, 0, "01000000");
  gSimFileSystem[SimFileCommand(192, 28618, 0, 0, 15)] =
      SimFileResponse(144, 0, "0000000a6fca040011a0aa01020105");
  gSimFileSystem[SimFileCommand(178, 28618, 1, 4, 5)] =
      SimFileResponse(144, 0, "0000000000");
  gSimFileSystem[SimFileCommand(192, 28589, 0, 0, 15)] =
      SimFileResponse(144, 0, "000000046fad04000aa0aa01020000");
  gSimFileSystem[SimFileCommand(176, 28589, 0, 0, 4)] =
      SimFileResponse(144, 0, "00000003");
  gSimFileSystem[SimFileCommand(192, 28438, 0, 0, 15)] =
      SimFileResponse(144, 0, "000000026f1604001aa0aa01020000");
  gSimFileSystem[SimFileCommand(176, 28438, 0, 0, 2)] =
      SimFileResponse(144, 0, "0233");
  gSimFileSystem[SimFileCommand(192, 28486, 0, 0, 15)] =
      SimFileResponse(148, 4, NULL);
  gSimFileSystem[SimFileCommand(192, 28621, 0, 0, 15)] =
      SimFileResponse(148, 4, NULL);
  gSimFileSystem[SimFileCommand(192, 28613, 0, 0, 15)] =
      SimFileResponse(144, 0, "000000f06fc504000aa0aa01020118");
  gSimFileSystem[SimFileCommand(178, 28613, 1, 4, 24)] = SimFileResponse(
      144, 0, "43058441aa890affffffffffffffffffffffffffffffffff");
  gSimFileSystem[SimFileCommand(192, 28480, 0, 0, 15)] =
      SimFileResponse(144, 0, "000000806f40040011a0aa01020120");
  // Primary phone number encapsulated
  // [51][55][21][43][65][f7] = 1 555 1234 567$
  gSimFileSystem[SimFileCommand(178, 28480, 1, 4, 32)] = SimFileResponse(
      144, 0,
      "ffffffffffffffffffffffffffffffffffff07915155214365f7ffffffffffff");
  gSimFileSystem[SimFileCommand(192, 28615, 0, 0, 15)] =
      SimFileResponse(144, 0, "000000406fc7040011a0aa01020120");
  // Voice mail number encapsulated
  // [56][6f][69][63][65][6d][61][69][6c] = 'Voicemail'
  // [51][55][67][45][23][f1] = 1 555 7654 321$
  gSimFileSystem[SimFileCommand(178, 28615, 1, 4, 32)] = SimFileResponse(
      144, 0,
      "566f6963656d61696cffffffffffffffffff07915155674523f1ffffffffffff");
  gSimFileSystem[SimFileCommand(192, 12037, 0, 0, 15)] =
      SimFileResponse(148, 4, NULL);
  gSimFileSystem[SimFileCommand(192, 28437, 0, 0, 15)] =
      SimFileResponse(148, 4, NULL);
  gSimFileSystem[SimFileCommand(192, 28478, 0, 0, 15)] =
      SimFileResponse(148, 4, NULL);
  gSimFileSystem[SimFileCommand(192, 28450, 0, 0, 15)] =
      SimFileResponse(148, 4, NULL);
  gSimFileSystem[SimFileCommand(192, 28456, 0, 0, 15)] =
      SimFileResponse(148, 4, NULL);
  gSimFileSystem[SimFileCommand(192, 28474, 0, 0, 15)] =
      SimFileResponse(148, 4, NULL);
  gSimFileSystem[SimFileCommand(192, 28481, 0, 0, 15)] =
      SimFileResponse(148, 4, NULL);
  gSimFileSystem[SimFileCommand(192, 28484, 0, 0, 15)] =
      SimFileResponse(148, 4, NULL);
  gSimFileSystem[SimFileCommand(192, 28493, 0, 0, 15)] =
      SimFileResponse(148, 4, NULL);
  gSimFileSystem[SimFileCommand(192, 28619, 0, 0, 15)] =
      SimFileResponse(148, 4, NULL);
  gSimFileSystem[SimFileCommand(176, 28506, 0, 0, 4)] =
      SimFileResponse(144, 0, "00000013");
}

static void request_SIM_IO(void* data, size_t /*datalen*/, RIL_Token t) {
  const RIL_SIM_IO_v6& args = *(RIL_SIM_IO_v6*)data;
  RIL_SIM_IO_Response sr = {0, 0, 0};

  ALOGV(
      "Requesting SIM File IO: %d EFID %x, Params: %d, %d, %d, path: %s, "
      "data %s PIN: %s AID: %s",
      args.command, args.fileid, args.p1, args.p2, args.p3, args.path,
      args.data, args.pin2, args.aidPtr);

  SimFileCommand cmd(args.command, args.fileid, args.p1, args.p2, args.p3);

  std::map<SimFileCommand, SimFileResponse>::iterator resp =
      gSimFileSystem.find(cmd);

  if (resp != gSimFileSystem.end()) {
    sr.sw1 = resp->second.sw1;
    sr.sw2 = resp->second.sw2;
    if (resp->second.data) sr.simResponse = strdup(resp->second.data);
    gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &sr, sizeof(sr));
    return;
  }

  ALOGW("Unsupported SIM File IO command.");
  gce_ril_env->OnRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

static void request_enter_sim_pin(void* data, size_t /*datalen*/, RIL_Token t) {
  const char** pin_aid = (const char**)data;

  ALOGV("Entering PIN: %s / %s", pin_aid[0], pin_aid[1]);

  ++gSimPINAttempts;
  int remaining_attempts = gSimPINAttemptsMax - gSimPINAttempts;

  bool is_valid = false;

  if (gSimStatus == SIM_PIN) {
    is_valid = (gSimPIN == pin_aid[0]);
  } else if (gSimStatus == SIM_PUK) {
    is_valid = (gSimPUK == pin_aid[0]);
  } else {
    ALOGV("Unexpected SIM status for unlock: %d", gSimStatus);
    gce_ril_env->OnRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    return;
  }

  if (!is_valid) {
    if (gSimPINAttempts == gSimPINAttemptsMax) {
      if (gSimStatus == SIM_PIN) {
        gSimStatus = SIM_PUK;
        gSimPINAttempts = 0;
      } else {
        ALOGV("PIN and PUK verification failed; locking SIM card.");
        gSimStatus = SIM_NOT_READY;
        gce_ril_env->OnRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        return;
      }
    }

    gce_ril_env->OnRequestComplete(t, RIL_E_PASSWORD_INCORRECT,
                                   &remaining_attempts,
                                   sizeof(remaining_attempts));
  } else {
    if (gSimStatus == SIM_PUK) {
      ALOGV("Resetting SIM PIN to %s", pin_aid[1]);
      gSimPIN = pin_aid[1];
    }

    gSimPINAttempts = 0;
    gSimStatus = SIM_READY;
    gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &remaining_attempts,
                                   sizeof(remaining_attempts));
  }

  pollSIMState(NULL);
}

/**
 * No longer POLL.
 */
static void pollSIMState(void* /*param*/) {
  // TODO(ender): check radio state?

  ALOGV("Polling SIM Status.");

  switch (gSimStatus) {
    case SIM_ABSENT:
    case SIM_PIN:
    case SIM_PUK:
    case SIM_NETWORK_PERSONALIZATION:
    default:
      ALOGV("SIM Absent or Locked");
      break;

    case SIM_NOT_READY:
      // Transition directly to READY. Set default network operator.
      if (gRadioPowerState == RADIO_STATE_ON) {
        gSimStatus = SIM_READY;
        gCurrentNetworkOperator = "311740";
      }

      gce_ril_env->RequestTimedCallback(pollSIMState, NULL, &TIMEVAL_SIMPOLL);
      break;

    case SIM_READY:
      ALOGV("SIM Ready. Notifying network state changed.");
      break;
  }

  if (gRadioPowerState != RADIO_STATE_OFF) {
    gce_ril_env->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED,
                                       NULL, 0);
    gce_ril_env->OnUnsolicitedResponse(
        RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED, NULL, 0);
  }
}

std::map<SIM_Status, RIL_AppStatus> gRilAppStatus;

static void init_sim_status() {
  gRilAppStatus[SIM_ABSENT] = (RIL_AppStatus){RIL_APPTYPE_UNKNOWN,
                                              RIL_APPSTATE_UNKNOWN,
                                              RIL_PERSOSUBSTATE_UNKNOWN,
                                              NULL,
                                              NULL,
                                              0,
                                              RIL_PINSTATE_UNKNOWN,
                                              RIL_PINSTATE_UNKNOWN};
  gRilAppStatus[SIM_NOT_READY] =
      (RIL_AppStatus){RIL_APPTYPE_SIM,
                      RIL_APPSTATE_DETECTED,
                      RIL_PERSOSUBSTATE_UNKNOWN,
                      NULL,
                      NULL,
                      0,
                      RIL_PINSTATE_ENABLED_NOT_VERIFIED,
                      RIL_PINSTATE_ENABLED_NOT_VERIFIED};
  gRilAppStatus[SIM_READY] = (RIL_AppStatus){
      RIL_APPTYPE_SIM,
      RIL_APPSTATE_READY,
      RIL_PERSOSUBSTATE_READY,
      NULL,
      NULL,
      0,
      RIL_PINSTATE_ENABLED_VERIFIED,
      RIL_PINSTATE_ENABLED_VERIFIED,
  };
  gRilAppStatus[SIM_PIN] = (RIL_AppStatus){RIL_APPTYPE_SIM,
                                           RIL_APPSTATE_PIN,
                                           RIL_PERSOSUBSTATE_UNKNOWN,
                                           NULL,
                                           NULL,
                                           0,
                                           RIL_PINSTATE_ENABLED_NOT_VERIFIED,
                                           RIL_PINSTATE_UNKNOWN};
  gRilAppStatus[SIM_PUK] = (RIL_AppStatus){RIL_APPTYPE_SIM,
                                           RIL_APPSTATE_PUK,
                                           RIL_PERSOSUBSTATE_UNKNOWN,
                                           NULL,
                                           NULL,
                                           0,
                                           RIL_PINSTATE_ENABLED_BLOCKED,
                                           RIL_PINSTATE_UNKNOWN};
  gRilAppStatus[SIM_NETWORK_PERSONALIZATION] =
      (RIL_AppStatus){RIL_APPTYPE_SIM,
                      RIL_APPSTATE_SUBSCRIPTION_PERSO,
                      RIL_PERSOSUBSTATE_SIM_NETWORK,
                      NULL,
                      NULL,
                      0,
                      RIL_PINSTATE_ENABLED_NOT_VERIFIED,
                      RIL_PINSTATE_UNKNOWN};
  gRilAppStatus[RUIM_ABSENT] = (RIL_AppStatus){RIL_APPTYPE_UNKNOWN,
                                               RIL_APPSTATE_UNKNOWN,
                                               RIL_PERSOSUBSTATE_UNKNOWN,
                                               NULL,
                                               NULL,
                                               0,
                                               RIL_PINSTATE_UNKNOWN,
                                               RIL_PINSTATE_UNKNOWN};
  gRilAppStatus[RUIM_NOT_READY] = (RIL_AppStatus){RIL_APPTYPE_RUIM,
                                                  RIL_APPSTATE_DETECTED,
                                                  RIL_PERSOSUBSTATE_UNKNOWN,
                                                  NULL,
                                                  NULL,
                                                  0,
                                                  RIL_PINSTATE_UNKNOWN,
                                                  RIL_PINSTATE_UNKNOWN};
  gRilAppStatus[RUIM_READY] = (RIL_AppStatus){RIL_APPTYPE_RUIM,
                                              RIL_APPSTATE_READY,
                                              RIL_PERSOSUBSTATE_READY,
                                              NULL,
                                              NULL,
                                              0,
                                              RIL_PINSTATE_UNKNOWN,
                                              RIL_PINSTATE_UNKNOWN};
  gRilAppStatus[RUIM_PIN] = (RIL_AppStatus){RIL_APPTYPE_RUIM,
                                            RIL_APPSTATE_PIN,
                                            RIL_PERSOSUBSTATE_UNKNOWN,
                                            NULL,
                                            NULL,
                                            0,
                                            RIL_PINSTATE_ENABLED_NOT_VERIFIED,
                                            RIL_PINSTATE_UNKNOWN};
  gRilAppStatus[RUIM_PUK] = (RIL_AppStatus){RIL_APPTYPE_RUIM,
                                            RIL_APPSTATE_PUK,
                                            RIL_PERSOSUBSTATE_UNKNOWN,
                                            NULL,
                                            NULL,
                                            0,
                                            RIL_PINSTATE_ENABLED_BLOCKED,
                                            RIL_PINSTATE_UNKNOWN};
  gRilAppStatus[RUIM_NETWORK_PERSONALIZATION] =
      (RIL_AppStatus){RIL_APPTYPE_RUIM,
                      RIL_APPSTATE_SUBSCRIPTION_PERSO,
                      RIL_PERSOSUBSTATE_SIM_NETWORK,
                      NULL,
                      NULL,
                      0,
                      RIL_PINSTATE_ENABLED_NOT_VERIFIED,
                      RIL_PINSTATE_UNKNOWN};
}

/**
 * Get the current card status.
 */
static void getCardStatus(RIL_Token t) {
  ALOGV("Querying SIM status.");
  RIL_CardStatus_v6 card_status;

  if (gSimStatus == SIM_ABSENT) {
    card_status.card_state = RIL_CARDSTATE_ABSENT;
    card_status.num_applications = 0;
  } else {
    card_status.card_state = RIL_CARDSTATE_PRESENT;
    card_status.num_applications = 1;
  }

  card_status.universal_pin_state = RIL_PINSTATE_UNKNOWN;
  card_status.gsm_umts_subscription_app_index = -1;
  card_status.cdma_subscription_app_index = -1;
  card_status.ims_subscription_app_index = -1;

  // Initialize application status
  for (int i = 0; i < RIL_CARD_MAX_APPS; i++) {
    card_status.applications[i] = gRilAppStatus[SIM_ABSENT];
  }

  if (card_status.num_applications > 0) {
    card_status.gsm_umts_subscription_app_index = 0;

    card_status.applications[0] = gRilAppStatus[gSimStatus];
    card_status.universal_pin_state = card_status.applications[0].pin1;
    // To enable basic CDMA (currently neither supported nor functional):
    //    card_status.num_applications = 2;
    //    card_status.cdma_subscription_app_index = 1;
    //    card_status.applications[1] =
    //        gRilAppStatus[SIM_Status(gSimStatus + RUIM_ABSENT)];
  }

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &card_status,
                                 sizeof(card_status));
}

struct SimSession {
  std::string aid;
};

static int gNextSimSessionId = 1;
static std::map<int, SimSession> gSimSessions;

static void request_sim_open_channel(void* data, size_t /*datalen*/,
                                     RIL_Token t) {
  char* aid = (char*)data;
  SimSession session;

  ALOGV("Requesting new SIM session");

  if (aid != NULL) {
    session.aid = aid;
  }

  int response = gNextSimSessionId++;
  gSimSessions[response] = session;

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(response));
}

static void request_sim_close_channel(void* data, size_t /*datalen*/,
                                      RIL_Token t) {
  int session = *(int*)(data);

  ALOGV("Closing SIM session %d", session);

  if (gSimSessions.erase(session) == 0) {
    // No such session.
    gce_ril_env->OnRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
  } else {
    gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  }
}

static void request_sim_apdu(void* data, size_t /*datalen*/, RIL_Token t) {
  RIL_SIM_APDU* apdu = (RIL_SIM_APDU*)data;

  ALOGV("Requesting APDU: Session %d CLA %d INST %d Params: %d %d %d, data %s",
        apdu->sessionid, apdu->cla, apdu->instruction, apdu->p1, apdu->p2,
        apdu->p3, apdu->data);

  if (gSimSessions.find(apdu->sessionid) != gSimSessions.end()) {
    RIL_SIM_IO_Response sr{};

    // Fallback / default behavior.
    sr.sw1 = 144;
    sr.sw2 = 0;
    sr.simResponse = NULL;

    gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &sr, sizeof(sr));
  } else {
    gce_ril_env->OnRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
  }
}

// 0 = Lock is available, but disabled.
// 1 = Lock is available and enabled,
// 2 = lock is neither available nor enabled
static const int kFacilityLockAllDisabled = 0;

static void request_facility_lock(void* data, size_t /*datalen*/, RIL_Token t) {
  char** data_vec = (char**)data;

  // TODO(ender): implement this; essentially: AT+CLCK
  // See http://www.activexperts.com/sms-component/at/commands/?at=%2BCLCK
  // and
  // opt/telephony/src/java/com/android/internal/telephony/CommandsInterface.java
  // opt/telephony/src/java/com/android/internal/telephony/uicc/UiccCardApplication.java

  ALOGV("Query Facility Lock Code: %s PIN2: %s Service(s): %s AID: %s",
        data_vec[0], data_vec[1], data_vec[2], data_vec[3]);

  // TODO(ender): there should be a bit vector of responses for each of the
  // services requested.
  // Depending on lock code, facilities may be unlocked or locked. We report
  // these are all unlocked, regardless of the query.
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS,
                                 const_cast<int*>(&kFacilityLockAllDisabled),
                                 sizeof(kFacilityLockAllDisabled));
}

static void request_international_subscriber_id_number(RIL_Token t) {
  // TODO(ender): Reuse MCC and MNC.
  std::string subscriber_id = gCurrentNetworkOperator.c_str();
  subscriber_id += "123456789";

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS,
                                 strdup(subscriber_id.c_str()), sizeof(char*));
}

static bool gScreenIsOn = true;

static void request_set_screen_state(void* data, size_t /*datalen*/,
                                     RIL_Token t) {
  gScreenIsOn = *(int*)data ? true : false;
  ALOGV("Screen is %s", gScreenIsOn ? "on" : "off");
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

// Unsure which section this belongs in.

static int gModemTtyMode = 1;  // 0 = off, 1 = full, 2 = HCO, 3 = VCO.
static void request_set_tty_mode(void* data, size_t /*datalen*/, RIL_Token t) {
  int new_tty_mode = *(int*)(data);
  ALOGV("Switching modem TTY mode %d -> %d", gModemTtyMode, new_tty_mode);

  if (new_tty_mode >= 0 && new_tty_mode <= 3) {
    gModemTtyMode = new_tty_mode;
    gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  } else {
    gce_ril_env->OnRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
  }
}

static void request_get_tty_mode(RIL_Token t) {
  ALOGV("Querying TTY mode");
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &gModemTtyMode,
                                 sizeof(gModemTtyMode));
}

static bool gImsRegistered = false;
static int gImsFormat = RADIO_TECH_3GPP;

static void request_ims_registration_state(RIL_Token t) {
  ALOGV("Querying IMS mode");
  int reply[2];
  reply[0] = gImsRegistered;
  reply[1] = gImsFormat;

  ALOGV("Requesting IMS Registration state: %d, format=%d ", reply[0],
        reply[1]);

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, reply, sizeof(reply));
}

// New functions after P.
static void request_start_network_scan(RIL_Token t) {
  ALOGV("Scanning network - void");
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  return;
}

static void request_set_preferred_network_type_bitmap(int /*request*/, void* data,
                                               size_t /*datalen*/,
                                               RIL_Token t) {
  RIL_RadioAccessFamily desired_access = *(RIL_RadioAccessFamily*)(data);

  ALOGV("Requesting modem technology change %d -> %d", default_access, desired_access);

  /** TODO future implementation: set modem type based on radio access family.
   * 1) find supported_technologies and desired_technologies
   * 2) return RIL_E_MODE_NOT_SUPPORTED error if not supported
   */
  default_access = desired_access;
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  return;
}

static void request_get_preferred_network_type_bitmap(int /*request*/, void* /*data*/,
                                               size_t /*datalen*/,
                                               RIL_Token t) {
  ALOGV("Requesting modem radio access family: %d", default_access);
  gce_ril_env->OnRequestComplete(
      t, RIL_E_SUCCESS, (RIL_RadioAccessFamily*)(&default_access), sizeof(default_access));
}

static void request_emergency_dial(int /*request*/, void* /*data*/, size_t /*datalen*/,
    RIL_Token t) {
  ALOGV("Emergency dial");
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  return;
}

static void request_set_sim_card_power(int /*request*/, void* /*data*/, size_t /*datalen*/,
    RIL_Token t) {
  ALOGV("Set sim card power - void");
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  return;
}

static void request_get_modem_stack_status(int /*request*/, RIL_Token t) {
  ALOGV("Getting modem stack status - void");
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  return;
}

static void request_enable_modem(int /*request*/, RIL_Token t) {
  ALOGV("Enabling modem - void");
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  return;
}

static void request_set_system_selection_channels(int /*request*/, RIL_Token t) {
  ALOGV("request_set_system_selection_channels - void");
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  return;
}

// New functions after Q.
static void request_set_signal_strength_reporting_criteria(int /*request*/, void* /*data*/,
                                                           size_t /*datalen*/, RIL_Token t) {
  ALOGV("request_set_signal_strength_reporting_criteria - void");
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  return;
}

static void request_set_link_capacity_reporting_criteria(int /*request*/, void* /*data*/,
                                                         size_t /*datalen*/, RIL_Token t) {
  ALOGV("request_set_link_capacity_reporting_criteria - void");
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  return;
}

static void request_enable_uicc_applications(int /*request*/, void* data,
                                             size_t datalen,
                                             RIL_Token t) {
  ALOGV("Enable uicc applications.");

  if (data == NULL || datalen != sizeof(int)) {
    gce_ril_env->OnRequestComplete(t, RIL_E_INTERNAL_ERR, NULL, 0);
    return;
  }

  bool enable = *(int *)(data) != 0;

  ALOGV("areUiccApplicationsEnabled change from %d to %d", areUiccApplicationsEnabled, enable);

  areUiccApplicationsEnabled = enable;

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

static void request_are_uicc_applications_enabled(int /*request*/, void* /*data*/,
                                                  size_t /*datalen*/,
                                                  RIL_Token t) {
  ALOGV("Getting whether uicc applications are enabled.");

  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &areUiccApplicationsEnabled, sizeof(bool));
}

static void request_enter_sim_depersonalization(RIL_Token t) {
  ALOGV("request_enter_sim_depersonalization - void");
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  return;
}

static void request_cdma_send_sms_expect_more(RIL_Token t) {
  ALOGV("request_cdma_send_sms_expect_more - void");
  gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
  return;
}

static void gce_ril_on_request(int request, void* data, size_t datalen,
                               RIL_Token t) {
  // Ignore all requests except RIL_REQUEST_GET_SIM_STATUS
  // when RADIO_STATE_UNAVAILABLE.
  if (gRadioPowerState == RADIO_STATE_UNAVAILABLE &&
      request != RIL_REQUEST_GET_SIM_STATUS) {
    gce_ril_env->OnRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
    return;
  }

  // Ignore all non-power requests when RADIO_STATE_OFF.
  if (gRadioPowerState == RADIO_STATE_OFF) {
    switch (request) {
      case RIL_REQUEST_GET_SIM_STATUS:
      case RIL_REQUEST_OPERATOR:
      case RIL_REQUEST_RADIO_POWER:
      case RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE:
        // Process all the above, even though the radio is off
        break;
      default:
        gce_ril_env->OnRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
        return;
    }
  }

  ALOGV("Received request %d", request);

  switch (request) {
    case RIL_REQUEST_QUERY_AVAILABLE_NETWORKS:
      request_query_available_networks(data, datalen, t);
      break;
    case RIL_REQUEST_GET_IMEI:
      request_get_imei(t);
      break;
    case RIL_REQUEST_GET_IMEISV:
      request_get_imei_sv(t);
      break;
    case RIL_REQUEST_DEACTIVATE_DATA_CALL:
      request_teardown_data_call(data, datalen, t);
      break;
    case RIL_REQUEST_SCREEN_STATE:
      request_set_screen_state(data, datalen, t);
      break;
    case RIL_REQUEST_GET_SIM_STATUS:
      getCardStatus(t);
      break;
    case RIL_REQUEST_GET_CURRENT_CALLS:
      request_get_current_calls(data, datalen, t);
      break;
    case RIL_REQUEST_DIAL:
      request_dial(data, datalen, t);
      break;
    case RIL_REQUEST_HANGUP:
      request_hangup(data, datalen, t);
      break;
    case RIL_REQUEST_HANGUP_WAITING_OR_BACKGROUND:
      request_hangup_waiting(data, datalen, t);
      break;
    case RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND:
      request_hangup_current(t);
      break;
    case RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE:
      request_switch_current_and_waiting(t);
      break;
    case RIL_REQUEST_ANSWER:
      request_answer_incoming(t);
      break;
    case RIL_REQUEST_SET_MUTE:
      request_set_mute(data, datalen, t);
      break;
    case RIL_REQUEST_GET_MUTE:
      request_get_mute(t);
      break;
    case RIL_REQUEST_CONFERENCE:
      request_combine_multiparty_call(data, datalen, t);
      break;
    case RIL_REQUEST_SEPARATE_CONNECTION:
      request_split_multiparty_call(data, datalen, t);
      break;
    case RIL_REQUEST_UDUB:
      request_udub_on_incoming_calls(t);
      break;
    case RIL_REQUEST_SIGNAL_STRENGTH:
      request_signal_strength(data, datalen, t);
      break;
    case RIL_REQUEST_VOICE_REGISTRATION_STATE:
    case RIL_REQUEST_DATA_REGISTRATION_STATE:
      request_registration_state(request, data, datalen, t);
      break;
    case RIL_REQUEST_OPERATOR:
      request_operator(data, datalen, t);
      break;
    case RIL_REQUEST_RADIO_POWER:
      request_radio_power(data, datalen, t);
      break;
    case RIL_REQUEST_DTMF:
    case RIL_REQUEST_DTMF_START:
      request_send_dtmf(data, datalen, t);
      break;
    case RIL_REQUEST_DTMF_STOP:
      request_send_dtmf_stop(t);
      break;
    case RIL_REQUEST_SEND_SMS:
      request_send_SMS(data, t);
      break;
    case RIL_REQUEST_CDMA_SEND_SMS:
      request_cdma_send_SMS(data, t);
      break;
    case RIL_REQUEST_SETUP_DATA_CALL:
      request_setup_data_call(data, datalen, t);
      break;
    case RIL_REQUEST_SMS_ACKNOWLEDGE:
      request_SMS_acknowledge(data, datalen, t);
      break;
    case RIL_REQUEST_GET_IMSI:
      request_international_subscriber_id_number(t);
      break;
    case RIL_REQUEST_QUERY_FACILITY_LOCK:
      request_facility_lock(data, datalen, t);
      break;
    case RIL_REQUEST_SIM_IO:
      request_SIM_IO(data, datalen, t);
      break;
    case RIL_REQUEST_SEND_USSD:
      request_send_ussd(data, datalen, t);
      break;
    case RIL_REQUEST_CANCEL_USSD:
      request_cancel_ussd(t);
      break;
    case RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC:
      request_set_automatic_network_selection(t);
      break;
    case RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL:
      request_set_manual_network_selection(data, datalen, t);
      break;
    case RIL_REQUEST_DATA_CALL_LIST:
      request_data_calllist(data, datalen, t);
      break;
    case RIL_REQUEST_LAST_DATA_CALL_FAIL_CAUSE:
      request_datacall_fail_cause(t);
      break;
    case RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE:
      request_query_network_selection_mode(data, datalen, t);
      break;
    case RIL_REQUEST_OEM_HOOK_RAW:
    case RIL_REQUEST_OEM_HOOK_STRINGS:
      ALOGV("OEM Hooks not supported!");
      gce_ril_env->OnRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
      break;
    case RIL_REQUEST_WRITE_SMS_TO_SIM:
      request_write_sms_to_sim(data, datalen, t);
      break;
    case RIL_REQUEST_DELETE_SMS_ON_SIM:
      request_delete_sms_on_sim(data, datalen, t);
      break;
    case RIL_REQUEST_ENTER_SIM_PIN:
    case RIL_REQUEST_ENTER_SIM_PUK:
    case RIL_REQUEST_ENTER_SIM_PIN2:
    case RIL_REQUEST_ENTER_SIM_PUK2:
    case RIL_REQUEST_CHANGE_SIM_PIN:
    case RIL_REQUEST_CHANGE_SIM_PIN2:
      request_enter_sim_pin(data, datalen, t);
      break;
    case RIL_REQUEST_VOICE_RADIO_TECH: {
      RIL_RadioTechnology tech = getBestVoiceTechnology(gModemCurrentType);
      gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, &tech, sizeof(tech));
      break;
    }
    case RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE:
      request_set_preferred_network_type(request, data, datalen, t);
      break;
    case RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE:
      request_get_preferred_network_type(request, data, datalen, t);
      break;
    case RIL_REQUEST_GET_NEIGHBORING_CELL_IDS:
      request_get_neighboring_cell_ids(data, datalen, t);
      break;
    case RIL_REQUEST_GET_CELL_INFO_LIST:
      request_get_cell_info_list(data, datalen, t);
      break;
    case RIL_REQUEST_SET_UNSOL_CELL_INFO_LIST_RATE:
      request_set_cell_info_list_rate(data, datalen, t);
      break;
    case RIL_REQUEST_BASEBAND_VERSION:
      request_baseband_version(t);
      break;
    case RIL_REQUEST_SET_TTY_MODE:
      request_set_tty_mode(data, datalen, t);
      break;
    case RIL_REQUEST_QUERY_TTY_MODE:
      request_get_tty_mode(t);
      break;
    case RIL_REQUEST_GET_RADIO_CAPABILITY:
      request_get_radio_capability(t);
      break;
    case RIL_REQUEST_SET_RADIO_CAPABILITY:
      request_set_radio_capability(data, datalen, t);
      break;
    case RIL_REQUEST_SET_DATA_PROFILE:
      gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
      break;
    case RIL_REQUEST_GET_HARDWARE_CONFIG:
      request_hardware_config(t);
      break;
    case RIL_REQUEST_IMS_REGISTRATION_STATE:
      request_ims_registration_state(t);
      break;
    case RIL_REQUEST_SIM_TRANSMIT_APDU_CHANNEL:
      request_sim_apdu(data, datalen, t);
      break;
    case RIL_REQUEST_SIM_OPEN_CHANNEL:
      request_sim_open_channel(data, datalen, t);
      break;
    case RIL_REQUEST_SIM_CLOSE_CHANNEL:
      request_sim_close_channel(data, datalen, t);
      break;
    case RIL_REQUEST_IMS_SEND_SMS:
      request_ims_send_SMS(data, datalen, t);
      break;
    case RIL_REQUEST_SET_INITIAL_ATTACH_APN:
      ALOGW("INITIAL ATTACH APN");
      gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
      break;

// New requests after P.
    case RIL_REQUEST_START_NETWORK_SCAN:
      request_start_network_scan(t);
      break;
    case RIL_REQUEST_GET_MODEM_STACK_STATUS:
      request_get_modem_stack_status(request, t);
      break;
    case RIL_REQUEST_ENABLE_MODEM:
      request_enable_modem(request, t);
      break;
    case RIL_REQUEST_EMERGENCY_DIAL:
      request_emergency_dial(request, data, datalen, t);
      break;
    case RIL_REQUEST_SET_SIM_CARD_POWER:
      request_set_sim_card_power(request, data, datalen, t);
      break;
    case RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE_BITMAP:
      request_get_preferred_network_type_bitmap(request, data, datalen, t);
      break;
    case RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE_BITMAP:
      request_set_preferred_network_type_bitmap(request, data, datalen, t);
      break;
    case RIL_REQUEST_SET_SYSTEM_SELECTION_CHANNELS:
      request_set_system_selection_channels(request, t);
      break;
    case RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING:
      gce_ril_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
      break;
    case RIL_REQUEST_DEVICE_IDENTITY:
      request_device_identity(request, data, datalen, t);
      break;
    case RIL_REQUEST_CDMA_GET_SUBSCRIPTION_SOURCE:
      request_cdma_get_subscription_source(request, data, datalen, t);
      break;
    case RIL_REQUEST_CDMA_SUBSCRIPTION:
      request_cdma_subscription(request, data, datalen, t);
      break;
    case RIL_REQUEST_CDMA_SET_SUBSCRIPTION_SOURCE:
      request_cdma_set_subscription_source(request, data, datalen, t);
      break;
    case RIL_REQUEST_CDMA_QUERY_ROAMING_PREFERENCE:
      request_cdma_get_roaming_preference(request, data, datalen, t);
      break;
    case RIL_REQUEST_CDMA_SET_ROAMING_PREFERENCE:
      request_cdma_set_roaming_preference(request, data, datalen, t);
      break;
    case RIL_REQUEST_EXIT_EMERGENCY_CALLBACK_MODE:
      request_exit_emergency_mode(data, datalen, t);
      break;

// New requests after Q.
    case RIL_REQUEST_SET_SIGNAL_STRENGTH_REPORTING_CRITERIA:
      request_set_signal_strength_reporting_criteria(request, data, datalen, t);
      break;
    case RIL_REQUEST_SET_LINK_CAPACITY_REPORTING_CRITERIA:
      request_set_link_capacity_reporting_criteria(request, data, datalen, t);
      break;
    case RIL_REQUEST_ENABLE_UICC_APPLICATIONS:
      request_enable_uicc_applications(request, data, datalen, t);
      break;
    case RIL_REQUEST_ARE_UICC_APPLICATIONS_ENABLED:
      request_are_uicc_applications_enabled(request, data, datalen, t);
      break;
    case RIL_REQUEST_ENTER_SIM_DEPERSONALIZATION:
      request_enter_sim_depersonalization(t);
      break;
    case RIL_REQUEST_CDMA_SEND_SMS_EXPECT_MORE:
      request_cdma_send_sms_expect_more(t);
      break;
    default:
      ALOGE("Request %d not supported.", request);
      gce_ril_env->OnRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
      break;
  }
}

#define CUTTLEFISH_RIL_VERSION 6

static const RIL_RadioFunctions ril_callbacks = {
    CUTTLEFISH_RIL_VERSION,     gce_ril_on_request, gce_ril_current_state,
    gce_ril_on_supports, gce_ril_on_cancel,  gce_ril_get_version};

extern "C" {

const RIL_RadioFunctions* RIL_Init(const struct RIL_Env* env, int /*argc*/,
                                   char** /*argv*/) {
  time(&gce_ril_start_time);
  gce_ril_env = env;

  global_ril_config = cuttlefish::DeviceConfig::Get();
  if (!global_ril_config) {
    ALOGE("Failed to open device configuration!!!");
    return nullptr;
  }

  TearDownNetworkInterface();

  init_modem_supported_network_types();
  init_modem_technologies();
  init_virtual_network();
  init_sim_file_system();
  init_sim_status();

  return &ril_callbacks;
}

}  // extern "C"
