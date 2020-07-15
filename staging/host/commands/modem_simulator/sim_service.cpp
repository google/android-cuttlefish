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

#include <tinyxml2.h>

#include "host/libs/config/cuttlefish_config.h"
#include "common/libs/utils/files.h"

#include "sim_service.h"
#include "network_service.h"
#include "pdu_parser.h"

namespace cuttlefish {

const std::pair<int, int> kSimPinSizeRange(4, 8);
constexpr int kSimPukSize = 8;
constexpr int kSimPinMaxRetryTimes = 3;
constexpr int kSimPukMaxRetryTimes = 10;
static const std::string kDefaultPinCode = "1234";
static const std::string kDefaultPukCode = "12345678";

static const std::string MF_SIM       = "3F00";
static const std::string DF_TELECOM   = "7F10";
static const std::string DF_PHONEBOOK = "5F3A";
static const std::string DF_GRAPHICS  = "5F50";
static const std::string DF_GSM       = "7F20";
static const std::string DF_CDMA      = "7F25";
static const std::string DF_ADF       = "7FFF";  // UICC access

// In an ADN record, everything but the alpha identifier
// is in a footer that's 14 bytes
constexpr int kFooterSizeBytes = 14;
// Maximum size of the un-extended number field
constexpr int kMaxNumberSizeBytes = 11;

constexpr int kMaxLogicalChannels = 3;

const std::map<SimService::SimStatus, std::string> gSimStatusResponse = {
    {SimService::SIM_STATUS_ABSENT,     ModemService::kCmeErrorSimNotInserted},
    {SimService::SIM_STATUS_NOT_READY,  ModemService::kCmeErrorSimBusy},
    {SimService::SIM_STATUS_READY,      "+CPIN: READY"},
    {SimService::SIM_STATUS_PIN,        "+CPIN: SIM PIN"},
    {SimService::SIM_STATUS_PUK,        "+CPIN: SIM PUK"},
};

/* SimFileSystem */
XMLElement* SimService::SimFileSystem::GetRootElement() {
  return doc.RootElement();
}

std::string SimService::SimFileSystem::GetCommonIccEFPath(EFId efid) {
  switch (efid) {
    case EF_ADN:
    case EF_FDN:
    case EF_MSISDN:
    case EF_SDN:
    case EF_EXT1:
    case EF_EXT2:
    case EF_EXT3:
    case EF_PSI:
      return MF_SIM + DF_TELECOM;

    case EF_ICCID:
    case EF_PL:
      return MF_SIM;
    case EF_PBR:
      // we only support global phonebook.
      return MF_SIM + DF_TELECOM + DF_PHONEBOOK;
    case EF_IMG:
      return MF_SIM + DF_TELECOM + DF_GRAPHICS;
    default:
      return {};
  }
}

std::string SimService::SimFileSystem::GetUsimEFPath(EFId efid) {
  switch(efid) {
    case EF_SMS:
    case EF_EXT5:
    case EF_EXT6:
    case EF_MWIS:
    case EF_MBI:
    case EF_SPN:
    case EF_AD:
    case EF_MBDN:
    case EF_PNN:
    case EF_OPL:
    case EF_SPDI:
    case EF_SST:
    case EF_CFIS:
    case EF_MAILBOX_CPHS:
    case EF_VOICE_MAIL_INDICATOR_CPHS:
    case EF_CFF_CPHS:
    case EF_SPN_CPHS:
    case EF_SPN_SHORT_CPHS:
    case EF_FDN:
    case EF_SDN:
    case EF_EXT3:
    case EF_MSISDN:
    case EF_EXT2:
    case EF_INFO_CPHS:
    case EF_CSP_CPHS:
    case EF_GID1:
    case EF_GID2:
    case EF_LI:
    case EF_PLMN_W_ACT:
    case EF_OPLMN_W_ACT:
    case EF_HPLMN_W_ACT:
    case EF_EHPLMN:
    case EF_FPLMN:
    case EF_LRPLMNSI:
    case EF_HPPLMN:
      return MF_SIM + DF_ADF;

    case EF_PBR:
      // we only support global phonebook.
      return MF_SIM + DF_TELECOM + DF_PHONEBOOK;
    default:
      std::string path = GetCommonIccEFPath(efid);
      if (path.empty()) {
        // The EFids in USIM phone book entries are decided by the card manufacturer.
        // So if we don't match any of the cases above and if it's a USIM return
        // the phone book path.
        return MF_SIM + DF_TELECOM + DF_PHONEBOOK;
      }
      return path;
  }
}

XMLElement* SimService::SimFileSystem::FindAttribute(XMLElement *parent,
                                                     const std::string& attr_name,
                                                     const std::string& attr_value) {
  if (parent == nullptr) {
    return nullptr;
  }

  XMLElement* child = parent->FirstChildElement();
  while (child) {
    const XMLAttribute *attr = child->FindAttribute(attr_name.c_str());
    if (attr && attr->Value() == attr_value) {
      break;
    }
    child = child->NextSiblingElement();
  }
  return child;
};

XMLElement* SimService::SimFileSystem::AppendNewElement(XMLElement* parent,
                                                        const char* name) {
  auto element = doc.NewElement(name);
  parent->InsertEndChild(element);
  return element;
}

XMLElement* SimService::SimFileSystem::AppendNewElementWithText(
        XMLElement* parent, const char* name, const char* text) {
  auto element = doc.NewElement(name);
  auto xml_text = doc.NewText(text);
  element->InsertEndChild(xml_text);
  parent->InsertEndChild(element);
  return element;
}

/* PinStatus */
bool SimService::PinStatus::CheckPasswordValid(std::string_view password) {
  for (int i = 0; i < password.size(); i++) {
    int c = (int)password[i];
    if (c >= 48 && c <= 57) {
      continue;
    } else {
      return false;
    }
  }
  return true;
}

bool SimService::PinStatus::VerifyPIN(const std::string_view pin) {
  if (pin.size() < kSimPinSizeRange.first || pin.size() > kSimPinSizeRange.second) {
    return false;
  }

  if (!CheckPasswordValid(pin)) {
    return false;
  }

  if (pin_remaining_times_ <= 0) {
    return false;
  }

  std::string_view temp(pin_);
  if (pin == temp) {  // C++20 remove Operator!=
    pin_remaining_times_ = kSimPinMaxRetryTimes;
    return true;
  }

  pin_remaining_times_ -= 1;
  return false;
}

bool SimService::PinStatus::VerifyPUK(const std::string_view puk) {
  if (puk.size() != kSimPukSize) {
    return false;
  }

  if (!CheckPasswordValid(puk)) {
    return false;
  }

  if (puk_remaining_times_ <= 0) {
    return false;
  }

  std::string_view temp(puk_);
  if (puk == temp) {  // C++20 remove Operator!=
    pin_remaining_times_ = kSimPinMaxRetryTimes;
    puk_remaining_times_ = kSimPukMaxRetryTimes;
    return true;
  }

  puk_remaining_times_ -= 1;
  return false;
}

bool SimService::PinStatus::ChangePIN(ChangeMode mode,
                                      const std::string_view pin_or_puk,
                                      const std::string_view new_pin) {
  auto length = new_pin.length();
  if (length < kSimPinSizeRange.first || length > kSimPinSizeRange.second) {
    LOG(ERROR) << "Invalid digit number for PIN";
    return false;
  }

  bool result = false;
  if (mode == WITH_PIN) {  // using old pin to change pin
    result = VerifyPIN(pin_or_puk);
  } else if (mode == WITH_PUK) {  // using puk to change pin
    result = VerifyPUK(pin_or_puk);
  }

  if (!result) {
    LOG(ERROR) << "Incorrect PIN or PUK";
    return false;
  }

  if (!CheckPasswordValid(new_pin)) {
    return false;
  }

  std::string temp(new_pin);
  pin_ = temp;
  return true;
}

bool SimService::PinStatus::ChangePUK(const std::string_view puk,
                                      const std::string_view new_puk) {
  bool result = VerifyPUK(puk);
  if (!result) {
    LOG(ERROR) << "Incorrect PUK or no retry times";
    return false;
  }

  if (new_puk.length() != kSimPukSize) {
    LOG(ERROR) << "Invalid digit number for PUK";
    return false;
  }

  std::string temp(new_puk);
  puk_ = temp;
  return true;
};

SimService::SimService(int32_t service_id, ChannelMonitor* channel_monitor,
                       ThreadLooper* thread_looper)
    : ModemService(service_id, this->InitializeCommandHandlers(),
                   channel_monitor, thread_looper) {
  InitializeServiceState();
}

std::vector<CommandHandler> SimService::InitializeCommandHandlers() {
  std::vector<CommandHandler> command_handlers = {
      CommandHandler(
          "+CPIN?",
          [this](const Client& client) { this->HandleSIMStatusReq(client); }),
      CommandHandler("+CPIN=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleChangeOrEnterPIN(client, cmd);
                     }),
      CommandHandler("+CRSM=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleSIM_IO(client, cmd);
                     }),
      CommandHandler(
          "+CIMI",
          [this](const Client& client) { this->HandleGetIMSI(client); }),
      CommandHandler(
          "+CICCID",
          [this](const Client& client) { this->HandleGetIccId(client); }),
      CommandHandler("+CLCK=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleFacilityLock(client, cmd);
                     }),
      CommandHandler("+CCHO=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleOpenLogicalChannel(client, cmd);
                     }),
      CommandHandler("+CCHC=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleCloseLogicalChannel(client, cmd);
                     }),
      CommandHandler("+CGLA=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleTransmitLogicalChannel(client, cmd);
                     }),
      CommandHandler("+CPWD=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleChangePassword(client, cmd);
                     }),
      CommandHandler("+CPINR=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleQueryRemainTimes(client, cmd);
                     }),
      CommandHandler("+CCSS",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleCdmaSubscriptionSource(client, cmd);
                     }),
      CommandHandler("+WRMP",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleCdmaRoamingPreference(client, cmd);
                     }),
  };
  return (command_handlers);
}

void SimService::InitializeServiceState() {
  InitializeSimFileSystemAndSimState();

  InitializeFacilityLock();

  // Max logical channels: 3
  logical_channels_ = {
      LogicalChannel(1), LogicalChannel(2), LogicalChannel(kMaxLogicalChannels),
  };
}

void SimService::InitializeSimFileSystemAndSimState() {
  std::stringstream ss;
  ss << "iccprofile_for_sim" << service_id_ << ".xml";
  auto icc_profile_name = ss.str();

  auto config = cuttlefish::CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();
  auto icc_profile_path = instance.PerInstancePath(icc_profile_name.c_str());
  std::string file = icc_profile_path;

  if (!cuttlefish::FileExists(icc_profile_path) ||
      !cuttlefish::FileHasContent(icc_profile_path.c_str())) {
    ss.clear();
    ss.str("");
    ss << "etc/modem_simulator/files/iccprofile_for_sim" << service_id_ << ".xml";

    auto etc_file_path = cuttlefish::DefaultHostArtifactsPath(ss.str());
    if (!cuttlefish::FileExists(etc_file_path) || !cuttlefish::FileHasContent(etc_file_path)) {
      sim_status_ = SIM_STATUS_ABSENT;
      return;
    }
    file = etc_file_path;
  }

  sim_file_system_.file_path = icc_profile_path;
  auto err = sim_file_system_.doc.LoadFile(file.c_str());
  if (err != tinyxml2::XML_SUCCESS) {
    LOG(ERROR) << "Unable to load XML file '" << file << " ', error " << err;
    sim_status_ = SIM_STATUS_ABSENT;
    return;
  }

  XMLElement *root = sim_file_system_.GetRootElement();
  if (!root) {
    LOG(ERROR) << "Unable to find root element: IccProfile";
    sim_status_ = SIM_STATUS_ABSENT;
    return;
  }

  // Default value if iccprofile not configure pin state
  sim_status_ = SIM_STATUS_READY;
  pin1_status_.pin_ = kDefaultPinCode;
  pin1_status_.puk_ = kDefaultPukCode;
  pin1_status_.pin_remaining_times_ = kSimPinMaxRetryTimes;
  pin1_status_.puk_remaining_times_ = kSimPukMaxRetryTimes;
  pin2_status_.pin_ = kDefaultPinCode;
  pin2_status_.puk_ = kDefaultPukCode;
  pin2_status_.pin_remaining_times_ = kSimPinMaxRetryTimes;
  pin2_status_.puk_remaining_times_ = kSimPukMaxRetryTimes;

  XMLElement *pin_profile = root->FirstChildElement("PinProfile");
  if (pin_profile) {
    // Pin1 status
    auto pin_state = pin_profile->FirstChildElement("PINSTATE");
    if (pin_state) {
      std::string state = pin_state->GetText();
      if (state == "PINSTATE_ENABLED_NOT_VERIFIED") {
        sim_status_ = SIM_STATUS_PIN;
      } else if (state == "PINSTATE_ENABLED_BLOCKED") {
        sim_status_ = SIM_STATUS_PUK;
      }
    }
    auto pin_code = pin_profile->FirstChildElement("PINCODE");
    if (pin_code) pin1_status_.pin_ = pin_code->GetText();

    auto puk_code = pin_profile->FirstChildElement("PUKCODE");
    if (puk_code) pin1_status_.puk_ = puk_code->GetText();

    auto pin_remaining_times = pin_profile->FirstChildElement("PINREMAINTIMES");
    if (pin_remaining_times) {
      pin1_status_.pin_remaining_times_ = std::stoi(pin_remaining_times->GetText());
    }

    auto puk_remaining_times = pin_profile->FirstChildElement("PUKREMAINTIMES");
    if (puk_remaining_times) {
      pin1_status_.puk_remaining_times_ = std::stoi(puk_remaining_times->GetText());
    }

    // Pin2 status
    auto pin2_code = pin_profile->FirstChildElement("PIN2CODE");
    if (pin2_code) pin2_status_.pin_ = pin2_code->GetText();

    auto puk2_code = pin_profile->FirstChildElement("PUK2CODE");
    if (puk2_code) pin2_status_.puk_ = puk2_code->GetText();

    auto pin2_remaining_times = pin_profile->FirstChildElement("PIN2REMAINTIMES");
    if (pin2_remaining_times) {
      pin2_status_.pin_remaining_times_ = std::stoi(pin2_remaining_times->GetText());
    }

    auto puk2_remaining_times = pin_profile->FirstChildElement("PUK2REMAINTIMES");
    if (puk2_remaining_times) {
      pin2_status_.puk_remaining_times_ = std::stoi(puk2_remaining_times->GetText());
    }
  }
}

void SimService::InitializeFacilityLock() {
  /* Default disable */
  facility_lock_ = {
      {"SC", FacilityLock(FacilityLock::LockStatus::DISABLE)},
      {"FD", FacilityLock(FacilityLock::LockStatus::DISABLE)},
      {"AO", FacilityLock(FacilityLock::LockStatus::DISABLE)},
      {"OI", FacilityLock(FacilityLock::LockStatus::DISABLE)},
      {"OX", FacilityLock(FacilityLock::LockStatus::DISABLE)},
      {"AI", FacilityLock(FacilityLock::LockStatus::DISABLE)},
      {"IR", FacilityLock(FacilityLock::LockStatus::DISABLE)},
      {"AB", FacilityLock(FacilityLock::LockStatus::DISABLE)},
      {"AG", FacilityLock(FacilityLock::LockStatus::DISABLE)},
      {"AC", FacilityLock(FacilityLock::LockStatus::DISABLE)},
  };

  XMLElement *root = sim_file_system_.GetRootElement();
  if (!root) {
    LOG(ERROR) << "Unable to find root element: IccProfile";
    sim_status_ = SIM_STATUS_ABSENT;
    return;
  }

  XMLElement *facility_lock = root->FirstChildElement("FacilityLock");
  if (!facility_lock) {
    LOG(ERROR) << "Unable to find element: FacilityLock";
    return;
  }

  for (auto iter = facility_lock_.begin(); iter != facility_lock_.end(); ++iter) {
    auto lock_status = facility_lock->FirstChildElement(iter->first.c_str());
    if (lock_status) {
      std::string state = lock_status->GetText();
      if (state == "ENABLE") {
        iter->second.lock_status = FacilityLock::LockStatus::ENABLE;
      }
    }
  }
}

void SimService::SavePinStateToIccProfile() {
  XMLElement *root = sim_file_system_.GetRootElement();
  if (!root) {
    LOG(ERROR) << "Unable to find root element: IccProfile";
    sim_status_ = SIM_STATUS_ABSENT;
    return;
  }

  XMLElement *pin_profile = root->FirstChildElement("PinProfile");
  if (!pin_profile) {
    pin_profile = sim_file_system_.AppendNewElement(root, "PinProfile");
  }

  const char* text = "PINSTATE_UNKNOWN";

  if (sim_status_ == SIM_STATUS_PUK) {
    text = "PINSTATE_ENABLED_BLOCKED";
  } else {
    auto iter = facility_lock_.find("SC");
    if (iter != facility_lock_.end()) {
      if (iter->second.lock_status == FacilityLock::ENABLE) {
        text = "PINSTATE_ENABLED_NOT_VERIFIED";
      }
    }
  }

  // Pin1 status
  auto pin_state = pin_profile->FirstChildElement("PINSTATE");
  if (!pin_state) {
    pin_state = sim_file_system_.AppendNewElementWithText(pin_profile, "PINSTATE", text);
  } else {
    pin_state->SetText(text);
  }

  auto pin_code = pin_profile->FirstChildElement("PINCODE");
  if (!pin_code) {
    pin_code = sim_file_system_.AppendNewElementWithText(pin_profile, "PINCODE",
        pin1_status_.pin_.c_str());
  } else {
    pin_code->SetText(pin1_status_.pin_.c_str());
  }

  auto puk_code = pin_profile->FirstChildElement("PUKCODE");
  if (!puk_code) {
    puk_code = sim_file_system_.AppendNewElementWithText(pin_profile, "PUKCODE",
        pin1_status_.puk_.c_str());
  } else {
    puk_code->SetText(pin1_status_.puk_.c_str());
  }

  std::stringstream ss;
  ss << pin1_status_.pin_remaining_times_;

  auto pin_remaining_times = pin_profile->FirstChildElement("PINREMAINTIMES");
  if (!pin_remaining_times) {
    pin_remaining_times = sim_file_system_.AppendNewElementWithText(pin_profile,
        "PINREMAINTIMES", ss.str().c_str());
  } else {
    pin_remaining_times->SetText(ss.str().c_str());
  }
  ss.clear();
  ss.str("");
  ss << pin1_status_.puk_remaining_times_;

  auto puk_remaining_times = pin_profile->FirstChildElement("PUKREMAINTIMES");
  if (!puk_remaining_times) {
    puk_remaining_times = sim_file_system_.AppendNewElementWithText(pin_profile,
        "PUKREMAINTIMES", ss.str().c_str());
  } else {
    puk_remaining_times->SetText(ss.str().c_str());
  }

  // Pin2 status
  auto pin2_code = pin_profile->FirstChildElement("PIN2CODE");
  if (!pin2_code) {
    pin2_code = sim_file_system_.AppendNewElementWithText(pin_profile, "PIN2CODE",
        pin2_status_.pin_.c_str());
  } else {
    pin2_code->SetText(pin2_status_.pin_.c_str());
  }

  auto puk2_code = pin_profile->FirstChildElement("PUK2CODE");
  if (!puk2_code) {
    puk2_code = sim_file_system_.AppendNewElementWithText(pin_profile, "PUK2CODE",
        pin2_status_.puk_.c_str());
  } else {
    puk2_code->SetText(pin2_status_.puk_.c_str());
  }

  ss << pin2_status_.pin_remaining_times_;

  auto pin2_remaining_times = pin_profile->FirstChildElement("PIN2REMAINTIMES");
  if (!pin2_remaining_times) {
    pin2_remaining_times = sim_file_system_.AppendNewElementWithText(pin_profile,
        "PINREMAINTIMES", ss.str().c_str());
  } else {
    pin2_remaining_times->SetText(ss.str().c_str());
  }
  ss.clear();
  ss.str("");
  ss << pin2_status_.puk_remaining_times_;

  auto puk2_remaining_times = pin_profile->FirstChildElement("PUK2REMAINTIMES");
  if (!puk2_remaining_times) {
    puk2_remaining_times = sim_file_system_.AppendNewElementWithText(pin_profile,
        "PUK2REMAINTIMES", ss.str().c_str());
  } else {
    puk2_remaining_times->SetText(ss.str().c_str());
  }

  // Save file
  sim_file_system_.doc.SaveFile(sim_file_system_.file_path.c_str());
}

void SimService::SaveFacilityLockToIccProfile() {
  XMLElement *root = sim_file_system_.GetRootElement();
  if (!root) {
    LOG(ERROR) << "Unable to find root element: IccProfile";
    sim_status_ = SIM_STATUS_ABSENT;
    return;
  }

  XMLElement *facility_lock = root->FirstChildElement("FacilityLock");
  if (!facility_lock) {
    facility_lock = sim_file_system_.AppendNewElement(root, "FacilityLock");
  }

  const char* text = "DISABLE";

  for (auto iter = facility_lock_.begin(); iter != facility_lock_.end(); ++iter) {
    if (iter->second.lock_status == FacilityLock::LockStatus::ENABLE) {
      text = "ENABLE";
    } else {
      text = "DISABLE";
    }
    auto element = facility_lock->FirstChildElement(iter->first.c_str());
    if (!element) {
      element = sim_file_system_.AppendNewElementWithText(facility_lock,
          iter->first.c_str(), text);
    } else {
      element->SetText(text);
    }
  }

  sim_file_system_.doc.SaveFile(sim_file_system_.file_path.c_str());

  InitializeSimFileSystemAndSimState();
  InitializeFacilityLock();
}

bool SimService::IsFDNEnabled() {
  auto iter = facility_lock_.find("FD");
  if (iter != facility_lock_.end() &&
      iter->second.lock_status == FacilityLock::LockStatus::ENABLE) {
    return true;
  }
  return false;
}

bool SimService::IsFixedDialNumber(std::string_view number) {
  XMLElement *root = sim_file_system_.GetRootElement();
  if (!root) return false;

  auto path = SimFileSystem::GetUsimEFPath(SimFileSystem::EFId::EF_FDN);

  size_t pos = 0;
  auto parent = root;
  while (pos < path.length()) {
    std::string sub_path(path.substr(pos, 4));
    auto app = SimFileSystem::FindAttribute(parent, "path", sub_path);
    if (!app) return false;
    pos += 4;
    parent = app;
  }

  XMLElement* ef = SimFileSystem::FindAttribute(parent, "id", "6F3B");
  if (!ef) return false;

  XMLElement *final = ef->FirstChildElement("SIMIO");
  while (final) {
    std::string record = final->GetText();
    int footerOffset = record.length() - kFooterSizeBytes * 2;
    int numberLength = (record[footerOffset] - '0') * 16 +
                        record[footerOffset + 1] - '0';
    if (numberLength > kMaxNumberSizeBytes) {  // Invalid number length
      final = final->NextSiblingElement("SIMIO");
      continue;
    }

    std::string bcd_fdn = "";
    if (numberLength * 2 == 16) {  // Skip Type(91) and Country Code(68)
      bcd_fdn = record.substr(footerOffset + 6, numberLength * 2 - 4);
    } else {  // Skip Type(81)
      bcd_fdn = record.substr(footerOffset + 4, numberLength * 2 - 2);
    }

    std::string fdn = PDUParser::BCDToString(bcd_fdn);
    if (fdn == number) {
      return true;
    }
    final = final->NextSiblingElement("SIMIO");
  }

  return false;
}

XMLElement* SimService::GetIccProfile() {
  return sim_file_system_.GetRootElement();
}

std::string SimService::GetPhoneNumber() {
  XMLElement *root = sim_file_system_.GetRootElement();
  if (!root) return "";

  auto path = SimFileSystem::GetUsimEFPath(SimFileSystem::EFId::EF_MSISDN);

  size_t pos = 0;
  auto parent = root;
  while (pos < path.length()) {
    std::string sub_path(path.substr(pos, 4));
    auto app = SimFileSystem::FindAttribute(parent, "path", sub_path);
    if (!app) return "";
    pos += 4;
    parent = app;
  }

  XMLElement* ef = SimFileSystem::FindAttribute(parent, "id", "6F40");
  if (!ef) return "";

  XMLElement *final = SimFileSystem::FindAttribute(ef, "cmd", "B2");;
  if (!final) return "";

  std::string record = final->GetText();
  int footerOffset = record.length() - kFooterSizeBytes * 2;
  int numberLength = (record[footerOffset] - '0') * 16 +
                      record[footerOffset + 1] - '0';
  if (numberLength > kMaxNumberSizeBytes) {  // Invalid number length
    return "";
  }

  std::string bcd_number = "";
  if (numberLength * 2 == 16) {  // Skip Type(91) and Country Code(68)
    bcd_number = record.substr(footerOffset + 6, numberLength * 2 - 4);
  } else {  // Skip Type(81)
    bcd_number = record.substr(footerOffset + 4, numberLength * 2 - 2);
  }

  return PDUParser::BCDToString(bcd_number);
}

SimService::SimStatus SimService::GetSimStatus() const {
  return sim_status_;
}

std::string SimService::GetSimOperator() {
  XMLElement *root = sim_file_system_.GetRootElement();
  if (!root) return "";

  XMLElement* mf = SimFileSystem::FindAttribute(root, "path", MF_SIM);
  if (!mf) return "";

  XMLElement* df = SimFileSystem::FindAttribute(mf, "path", DF_ADF);
  if (!df) return "";

  XMLElement* ef = SimFileSystem::FindAttribute(df, "id", "6F07");
  if (!ef) return "";

  XMLElement *cimi = ef->FirstChildElement("CIMI");
  if (!cimi) return "";
  std::string imsi = cimi->GetText();

  ef = SimFileSystem::FindAttribute(df, "id", "6FAD");
  if (!ef) return "";

  XMLElement *sim_io = ef->FirstChildElement("SIMIO");
  while (sim_io) {
    const XMLAttribute *attr_cmd = sim_io->FindAttribute("cmd");
    std::string attr_value = attr_cmd ? attr_cmd->Value() : "";
    if (attr_cmd && attr_value == "B0") {
      break;
    }

    sim_io = sim_io->NextSiblingElement("SIMIO");
  }

  if (!sim_io) return "";

  std::string length = sim_io->GetText();
  int mnc_size = std::stoi(length.substr(length.size() -2));

  return imsi.substr(0, 3 + mnc_size);
}

void SimService::SetupDependency(NetworkService* net) {
  network_service_ = net;
}

/**
 * AT+CPIN
 *   Set command sends to the MT a password which is necessary before it can be
 * operated.
 *   Read command returns an alphanumeric string indicating whether some
 * password is required or not.
 *
 * Command                            Possible response(s)
 * +CPIN=<pin>[,<newpin>]              +CME ERROR: <err>
 * +CPIN?                              +CPIN: <code>
 *                                     +CME ERROR: <err>
 * <pin>, <newpin>: string type values.
 * <code> values reserved by the present document:
 *    READY   MT is not pending for any password
 *   SIM PIN  MT is waiting SIM PIN to be given
 *   SIM PUK  MT is waiting SIM PUK to be given
 *
 * see RIL_REQUEST_GET_SIM_STATUS in RIL
 */
void SimService::HandleSIMStatusReq(const Client& client) {
  std::vector<std::string> responses;
  auto iter = gSimStatusResponse.find(sim_status_);
  if (iter != gSimStatusResponse.end()) {
    responses.push_back(iter->second);
  } else {
    sim_status_ = SIM_STATUS_ABSENT;
    responses.push_back(kCmeErrorSimNotInserted);
  }
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/**
 * AT+CRSM
 *   By using this command instead of Generic SIM Access +CSIM TE application
 *   has easier but more limited access to the SIM database.
 *
 *   Command                                Possible response(s)
 * +CRSM=<command>[,<fileid>                +CRSM: <sw1>,<sw2>[,<response>]
 * [,<P1>,<P2>,<P3>[,<data>[,<pathid>]]]]   +CME ERROR: <err>
 *
 * <command>: (command passed on by the MT to the SIM; refer 3GPP TS 51.011 [28]):
 *   176 READ BINARY
 *   178 READ RECORD
 *   192 GET RESPONSE
 *   214 UPDATE BINARY
 *   220 UPDATE RECORD
 *   242 STATUS
 *   203 RETRIEVE DATA
 *   219 SET DATA
 *
 * <fileid>: integer type; this is the identifier of a elementary datafile on SIM.
 *           Mandatory for every command except STATUS.
 *
 * <P1>, <P2>, <P3>: integer type; parameters passed on by the MT to the SIM.
 *                   These parameters are mandatory for every command,
 *                   except GET RESPONSE and STATUS.
 *
 * <data>: information which shall be written to the SIM (hexadecimal character format).
 *
 * <pathid>: string type; contains the path of an elementary file on the SIM/UICC
 *           in hexadecimal format.
 *
 * <sw1>, <sw2>: integer type; information from the SIM about the execution of
 *               the actual command.
 *
 * <response>: response of a successful completion of the command previously issued
 *             (hexadecimal character format; refer +CSCS).
 */
void SimService::HandleSIM_IO(const Client& client,
                              const std::string& command) {
  std::vector<std::string> kFileNotFoud = {"+CRSM: 106,130", "OK"};
  std::vector<std::string> responses;

  CommandParser cmd(command);
  cmd.SkipPrefix();  // skip "AT+CRSM="

  auto c = cmd.GetNextStrDeciToHex();
  auto id = cmd.GetNextStrDeciToHex();
  auto p1 = cmd.GetNextStrDeciToHex();
  auto p2 = cmd.GetNextStrDeciToHex();
  auto p3 = cmd.GetNextStrDeciToHex();

  auto data = cmd.GetNextStr(',');
  std::string path(cmd.GetNextStr());

  XMLElement *root = sim_file_system_.GetRootElement();
  if (!root) {
    LOG(ERROR) << "Unable to find root element: IccProfile";
    client.SendCommandResponse(kCmeErrorOperationNotAllowed);
    return;
  }

  if (path == "") {
    SimFileSystem::EFId fileid = (SimFileSystem::EFId)std::stoi(id, nullptr, 16);
    path = SimFileSystem::GetUsimEFPath(fileid);
  }

  size_t pos = 0;
  auto parent = root;
  while (pos < path.length()) {
    std::string sub_path(path.substr(pos, 4));
    auto app = SimFileSystem::FindAttribute(parent, "path", sub_path);
    if (!app) {
      client.SendCommandResponse(kFileNotFoud);
      return;
    }
    pos += 4;
    parent = app;
  }

  XMLElement* ef = SimFileSystem::FindAttribute(parent, "id", id);
  if (!ef) {
    client.SendCommandResponse(kFileNotFoud);
    return;
  }

  XMLElement *final = ef->FirstChildElement("SIMIO");
  while (final) {
    const XMLAttribute *attr_cmd = final->FindAttribute("cmd");
    const XMLAttribute *attr_p1 = final->FindAttribute("p1");
    const XMLAttribute *attr_p2 = final->FindAttribute("p2");
    const XMLAttribute *attr_p3 = final->FindAttribute("p3");
    const XMLAttribute *attr_data = final->FindAttribute("data");

    if (c != "DC" && c != "D6") {  // Except UPDATE RECORD or UPDATE BINARY
      if ((attr_cmd && attr_cmd->Value() != c) ||
          (attr_data && attr_data->Value() != data)) {
        final = final->NextSiblingElement("SIMIO");
        continue;
      }
    }
    if (attr_p1 && attr_p1->Value() == p1 &&
        attr_p2 && attr_p2->Value() == p2 &&
        attr_p3 && attr_p3->Value() == p3) {
      break;
    }
    final = final->NextSiblingElement("SIMIO");
  }

  if (!final) {
    client.SendCommandResponse(kFileNotFoud);
    return;
  }

  std::string response = "+CRSM: ";
  if (c == "DC" || c == "D6") {
    std::string temp = "144,0,";
    temp += data;
    final->SetText(temp.c_str());
    sim_file_system_.doc.SaveFile(sim_file_system_.file_path.c_str());
    response.append("144,0");
  } else {
    response.append(final->GetText());
  }

  responses.push_back(response);
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

void SimService::OnSimStatusChanged() {
  auto ptr = network_service_;
  if (ptr) {
    ptr->OnSimStatusChanged(sim_status_);
  }
}

bool SimService::checkPin1AndAdjustSimStatus(std::string_view pin) {
  if (pin1_status_.VerifyPIN(pin) == true) {
    sim_status_ = SIM_STATUS_READY;
    OnSimStatusChanged();
    return true;
  }

  if (pin1_status_.pin_remaining_times_ <= 0) {
    sim_status_ = SIM_STATUS_PUK;
    OnSimStatusChanged();
  }

  return false;
}

bool SimService::ChangePin1AndAdjustSimStatus(PinStatus::ChangeMode mode,
                                              std::string_view pin,
                                              std::string_view new_pin) {
  if (pin1_status_.ChangePIN(mode, pin, new_pin) == true) {
    sim_status_ = SIM_STATUS_READY;
    OnSimStatusChanged();
    return true;
  }
  if (sim_status_ == SIM_STATUS_READY && pin1_status_.pin_remaining_times_ <= 0) {
    sim_status_ = SIM_STATUS_PIN;
    OnSimStatusChanged();
  } else if (sim_status_ == SIM_STATUS_PIN && pin1_status_.puk_remaining_times_ <= 0) {
    sim_status_ = SIM_STATUS_ABSENT;
    OnSimStatusChanged();
  }
  return false;
}

void SimService::HandleChangeOrEnterPIN(const Client& client,
                                        const std::string& command) {
  std::vector<std::string> responses;

  CommandParser cmd(command);
  cmd.SkipPrefix();  // skip "AT+CPIN="
  switch (sim_status_) {
    case SIM_STATUS_ABSENT:
      responses.push_back(kCmeErrorSimNotInserted);
      break;
    case SIM_STATUS_NOT_READY:
      responses.push_back(kCmeErrorSimBusy);
      break;
    case SIM_STATUS_READY: {
      /*
       * this may be a request to change the PIN with pin and new pin:
       *    AT+CPIN=pin,newpin
       * or a request to enter the PIN2
       *    AT+CPIN=pin2
       */
      auto pos = cmd->find(',');
      if (pos != std::string_view::npos) {  // change pin with new pin
        auto pin = cmd.GetNextStr(',');
        auto new_pin = *cmd;

        if (ChangePin1AndAdjustSimStatus(PinStatus::WITH_PIN, pin, new_pin)) {
          responses.push_back("OK");
        } else {
          responses.push_back(kCmeErrorIncorrectPassword);  /* incorrect PIN */
        }
      } else {  // verify pin2
        if (pin2_status_.VerifyPIN(*cmd) == true) {
          responses.push_back("OK");
        } else {
          responses.push_back(kCmeErrorIncorrectPassword);  /* incorrect PIN2 */
        }
      }
      break;
    }
    case SIM_STATUS_PIN: {  /* waiting for PIN */
      if (checkPin1AndAdjustSimStatus(*cmd) == true) {
        responses.push_back("OK");
      } else {
        responses.push_back(kCmeErrorIncorrectPassword);
      }
      break;
    }
    case SIM_STATUS_PUK: {
      /*
       * this may be a request to unlock the puk with new pin:
       *    AT+CPIN=puk,newpin
       */
      auto pos = cmd->find(',');
      if (pos != std::string_view::npos) {
        auto puk = cmd.GetNextStr(',');
        auto new_pin = *cmd;
        if (ChangePin1AndAdjustSimStatus(PinStatus::WITH_PUK, puk, new_pin)) {
          responses.push_back("OK");
        } else {
          responses.push_back(kCmeErrorIncorrectPassword);
        }
      } else {
        responses.push_back(kCmeErrorOperationNotAllowed);
      }
      break;
    }
    default:
      responses.push_back(kCmeErrorOperationNotAllowed);
      break;
  }

  client.SendCommandResponse(responses);
}

/**
 * AT+CIMI
 *   Execution command causes the TA to return <IMSI>, which is intended to
 * permit the TE to identify the individual SIM card or active application in
 * the UICC (GSM or USIM) which is attached to MT.
 *
 * Command                            Possible response(s)
 * +CIMI                               <IMSI>
 *                                     +CME ERROR: <err>
 *
 * <IMSI>: International Mobile Subscriber Identity (string without double quotes)
 *
 * see RIL_REQUEST_GET_IMSI in RIL
 */
void SimService::HandleGetIMSI(const Client& client) {
  std::vector<std::string> responses;

  XMLElement *root = sim_file_system_.GetRootElement();
  if (!root) {
    client.SendCommandResponse(kCmeErrorOperationNotAllowed);
    return;
  }

  XMLElement* mf = SimFileSystem::FindAttribute(root, "path", MF_SIM);
  if (!mf) {
    client.SendCommandResponse(kCmeErrorNotFound);
    return;
  }

  XMLElement* df = SimFileSystem::FindAttribute(mf, "path", DF_ADF);
  if (!df) {
    client.SendCommandResponse(kCmeErrorNotFound);
    return;
  }

  XMLElement* ef = SimFileSystem::FindAttribute(df, "id", "6F07");
  if (!ef) {
    client.SendCommandResponse(kCmeErrorNotFound);
    return;
  }

  XMLElement *final = ef->FirstChildElement("CIMI");
  if (!final) {
    client.SendCommandResponse(kCmeErrorNotFound);
    return;
  }

  responses.push_back(final->GetText());
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/**
 * AT+CICCID
 *   Integrated Circuit Card IDentifier (ICCID) is Unique Identifier of the SIM CARD.
 *  File is located in the SIM card at EFiccid (0x2FE2).
 *
 * see RIL_REQUEST_GET_SIM_STATUS in RIL
 */
void SimService::HandleGetIccId(const Client& client) {
  std::vector<std::string> responses;

  XMLElement *root = sim_file_system_.GetRootElement();
  if (!root) {
    client.SendCommandResponse(kCmeErrorOperationNotAllowed);
    return;
  }

  XMLElement* mf = SimFileSystem::FindAttribute(root, "path", MF_SIM);
  if (!mf) {
    client.SendCommandResponse(kCmeErrorNotFound);
    return;
  }

  XMLElement* ef = SimFileSystem::FindAttribute(mf, "id", "2FE2");
  if (!ef) {
    client.SendCommandResponse(kCmeErrorNotFound);
    return;
  }

  XMLElement *final = ef->FirstChildElement("CCID");
  if (!final) {
    client.SendCommandResponse(kCmeErrorNotFound);
    return;
  }

  responses.push_back(final->GetText());
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/*
 * AT+CLCK
 *   Execute command is used to lock, unlock or interrogate a MT or a network
 * facility <fac>.
 *
 * Command                            Possible response(s)
 * +CLCK=<fac>, <mode> [, <password>   OK or +CME ERROR: <err>
 *       [, <class>]]                  +CLCK: <status>[,<class1>[<CR><LF>+CLCK:
 *                                     <status>,<class2>[...]](when mode=2,it’s
 *                                     in inquiry status.)
 * <fac> values reserved by the present document:
 *    "SC": SIM (lock SIM/UICC card installed in the currently selected card
 *          slot) (SIM/UICC asks password in MT power‑up and when this lock
 *          command issued).
 *    "FD": SIM card or active application in the UICC (GSM or USIM) fixed
 *          dialling memory feature (if PIN2 authentication has not been done
 *          during the current session, PIN2 is required as <passwd>).
 * <mode>: integer type
 *      0: unlock
 *      1: lock
 *      2: query status
 * <status>: integer type
 *        0: not active
 *        1: active
 * <passwd>: string type; shall be the same as password specified for the
 *           facility from the MT user interface or with command
 *           Change Password +CPWD.
 * <classx> is a sum of integers each representing a class of information
 *          (default 7 - voice, data and fax):
 *        1 voice (telephony)
 *        2 data
 *        4 fax (facsimile services)
 *        8 short message service
 *       16 data circuit sync
 *       32 data circuit async
 *       64 dedicated packet access
 *      128 dedicated PAD access
 *
 * see RIL_REQUEST_SET_FACILITY_LOCK in RIL
 */
void SimService::HandleFacilityLock(const Client& client,
                                    const std::string& command) {
  CommandParser cmd(command);
  std::string lock(cmd.GetNextStr());
  int mode = cmd.GetNextInt();
  auto password = cmd.GetNextStr();
  // Ignore class from RIL

  auto iter = facility_lock_.find(lock);
  if (iter == facility_lock_.end()) {
    client.SendCommandResponse(kCmeErrorOperationNotSupported);
    return;
  }

  std::stringstream ss;
  std::vector<std::string> responses;
  switch (mode) {
    case FacilityLock::Mode::QUERY: {
      ss << "+CLCK: " << iter->second.lock_status;
      responses.push_back(ss.str());
      responses.push_back("OK");
      break;
    }
    case FacilityLock::Mode::LOCK:
    case FacilityLock::Mode::UNLOCK: {
      if (lock == "SC") {
        if (checkPin1AndAdjustSimStatus(password) == true) {
          iter->second.lock_status = (FacilityLock::LockStatus)mode;
          responses.push_back("OK");
        } else {
          responses.push_back(kCmeErrorIncorrectPassword);
        }
      } else if (lock == "FD") {
        if (pin2_status_.VerifyPIN(password) == true) {
          iter->second.lock_status = (FacilityLock::LockStatus)mode;
          responses.push_back("OK");
        } else {
          responses.push_back(kCmeErrorIncorrectPassword);
        }
      } else {  // Don't need password except 'SC' and 'FD'
        iter->second.lock_status = (FacilityLock::LockStatus)mode;
        responses.push_back("OK");
      }
      break;
    }
    default:
      responses.push_back(kCmeErrorInCorrectParameters);
      break;
  }

  client.SendCommandResponse(responses);
}

/**
 * AT+CCHO
 *   The currently selected UICC will open a new logical channel; select the
 * application identified by the <dfname> received with this command and return
 * a session Id as the response.
 *
 * Command                            Possible response(s)
 * +CCHO=<dfname>                      <sessionid>
 *                                     +CME ERROR: <err>
 *
 * <dfname>: all selectable applications in the UICC are referenced by a DF
 *           name coded on 1 to 16 bytes.
 * <sessionid>: integer type; a session Id to be used in order to target a
 *            specific application on the smart card (e.g. (U)SIM, WIM, ISIM)
 *            using logical channels mechanism.
 *
 * see RIL_REQUEST_SIM_OPEN_CHANNEL in RIL
 */
void SimService::HandleOpenLogicalChannel(const Client& client,
                                          const std::string& command) {
  std::vector<std::string> responses;

  CommandParser cmd(command);
  cmd.SkipPrefix();  // skip AT+CCHO=
  if (cmd->empty()) {
    client.SendCommandResponse(kCmeErrorInCorrectParameters);
    return;
  }

  std::vector<LogicalChannel>::iterator iter = logical_channels_.begin();
  for (; iter != logical_channels_.end(); ++iter) {
    if (!iter->is_open) break;
  }

  if (iter != logical_channels_.end()) {
    iter->is_open = true;
    iter->df_name = *cmd;

    std::stringstream ss;
    ss << iter->session_id;
    responses.push_back(ss.str());
    responses.push_back("OK");
  } else {
    responses.push_back(kCmeErrorMemoryFull);
  }

  client.SendCommandResponse(responses);
}

/**
 * AT+CCHC
 *   This command asks the ME to close a communication session with the active
 * UICC.
 *
 * Command                            Possible response(s)
 * +CCHC=<sessionid>                   +CCHC
 *                                     +CME ERROR: <err>
 * <sessionid>: see AT+CCHO
 *
 * see RIL_REQUEST_SIM_CLOSE_CHANNEL in RIL
 */
void SimService::HandleCloseLogicalChannel(const Client& client,
                                           const std::string& command) {
  std::vector<std::string> responses;

  CommandParser cmd(command);
  cmd.SkipPrefix();  // skip AT+CCHC=

  int session_id = cmd.GetNextInt();
  std::vector<LogicalChannel>::iterator iter = logical_channels_.begin();
  for (; iter != logical_channels_.end(); ++iter) {
    if (iter->session_id == session_id) break;
  }

  if (iter != logical_channels_.end()) {
    iter->is_open = false;
    iter->df_name.clear();
    responses.push_back("+CCHC");
    responses.push_back("OK");
  } else {
    responses.push_back(kCmeErrorNotFound);
  }
  client.SendCommandResponse(responses);
}

/**
 * AT+CGLA
 *   Set command transmits to the MT the <command> it then shall send as it is
 * to the selected UICC. In the same manner the UICC <response> shall be sent
 * back by the MT to the TA as it is.
 *
 * Command                            Possible response(s)
 * +CGLA=<sessionid>,<length>,         +CGLA: <length>,<response>
 *                                     +CME ERROR: <err>
 * <sessionid>: AT+CCHO
 * <length>: integer type; length of the characters that are sent to TE in
 *         <command> or <response> .
 * <command>: command passed on by the MT to the UICC in the format as described
 *          in 3GPP TS 31.101 [65] (hexadecimal character format; refer +CSCS).
 * <response>: response to the command passed on by the UICC to the MT in the
 *           format as described in 3GPP TS 31.101 [65] (hexadecimal character
 *           format; refer +CSCS).
 *
 * see RIL_REQUEST_SIM_TRANSMIT_APDU_CHANNEL in RIL
 */
void SimService::HandleTransmitLogicalChannel(const Client& client,
                                              const std::string& command) {
  std::vector<std::string> responses;

  CommandParser cmd(command);
  cmd.SkipPrefix();  // skip AT+CGLA=

  int session_id = cmd.GetNextInt();
  int length = cmd.GetNextInt();
  if (cmd->length() != length) {
    client.SendCommandResponse(kCmeErrorInCorrectParameters);
    return;
  }

  // Check if session id is opened
  auto iter = logical_channels_.begin();
  for (; iter != logical_channels_.end(); ++iter) {
    if (iter->session_id == session_id && iter->is_open) {
      break;
    }
  }

  if (iter == logical_channels_.end()) {
    client.SendCommandResponse(kCmeErrorInvalidIndex);
    return;
  }

  XMLElement *root = sim_file_system_.GetRootElement();
  if (!root) {
    client.SendCommandResponse(kCmeErrorOperationNotAllowed);
    return;
  }

  XMLElement* df = SimFileSystem::FindAttribute(root, "aid", iter->df_name);
  if (!df) {
    client.SendCommandResponse(kCmeErrorNotFound);
    return;
  }

  std::string attr_value(cmd->substr(2));  // skip session id
  XMLElement* final = SimFileSystem::FindAttribute(df, "CGLA", attr_value);
  if (!final) {
    client.SendCommandResponse(kCmeErrorNotFound);
    return;
  }

  responses.push_back(final->GetText());
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/**
 * AT+CPWD
 *   Action command sets a new password for the facility lock function defined
 * by command Facility Lock +CLCK
 *
 * Command                              Possible response(s)
 * +CPWD=<fac>,<oldpwd>,<newpwd>          +CME ERROR: <err>
 *
 * <fac>:
 *   "P2"  SIM PIN2
 *   refer Facility Lock +CLCK for other values
 * <oldpwd>, <newpwd>:
 *   string type; <oldpwd> shall be the same as password specified for the
 *   facility from the MT user interface or with command Change Password +CPWD
 *   and <newpwd> is the new password; maximum length of password can be determined
 *   with <pwdlength>
 * <pwdlength>: integer type maximum length of the password for the facility
 */
void SimService::HandleChangePassword(const Client& client,
                                      const std::string& command) {
  std::string response = kCmeErrorIncorrectPassword;

  CommandParser cmd(command);
  cmd.SkipPrefix();
  auto lock = cmd.GetNextStr();
  auto old_password = cmd.GetNextStr();
  auto new_password = cmd.GetNextStr();

  if (lock == "SC") {
    if (ChangePin1AndAdjustSimStatus(PinStatus::WITH_PIN, old_password, new_password)) {
      response = "OK";
    }
  } else if (lock == "P2" || lock == "FD") {
    if (pin2_status_.ChangePIN(PinStatus::WITH_PIN, old_password, new_password)) {
      response = "OK";
    }
  } else {
    response = kCmeErrorOperationNotSupported;;
  }

  client.SendCommandResponse(response);
}

/**
 * AT+CPINR
 *   Execution command cause the MT to return the number of remaining PIN retries
 * for the MT passwords with intermediate result code
 *
 * Command                        Possible response(s)
 * +CPINR[=<sel_code>]            +CPINR: <code>,<retries>[,<default_retries>]
 *
 * <retries>:
 *   integer type. Number of remaining retries per PIN.
 * <default_retries>:
 *   integer type. Number of default/initial retries per PIN.
 * <code>:
 *   Type of PIN. All values listed under the description of the AT+CPIN command
 * <sel_code>: String type. Same values as for the <code> and <ext_code> parameters.
 *   these values are strings and shall be indicated within double quotes.
 */
void SimService::HandleQueryRemainTimes(const Client& client,
                                        const std::string& command) {
  std::vector<std::string> responses;
  std::stringstream ss;

  CommandParser cmd(command);
  cmd.SkipPrefix();
  auto lock_type = cmd.GetNextStr();

  if (lock_type == "SIM PIN") {
    ss << "+CPINR: SIM PIN," << pin1_status_.pin_remaining_times_ << ","
                             << kSimPinMaxRetryTimes;
  } else if (lock_type == "SIM PUK") {
    ss << "+CPINR: SIM PUK," << pin1_status_.puk_remaining_times_ << ","
                             << kSimPukMaxRetryTimes;
  } else if (lock_type == "SIM PIN2") {
    ss << "+CPINR: SIM PIN2," << pin2_status_.pin_remaining_times_ << ","
                              << kSimPinMaxRetryTimes;
  } else if (lock_type == "SIM PUK2") {
    ss << "+CPINR: SIM PUK2," << pin2_status_.puk_remaining_times_ << ","
            << kSimPukMaxRetryTimes;
  } else {
    responses.push_back(kCmeErrorInCorrectParameters);
    client.SendCommandResponse(responses);
    return;
  }

  responses.push_back(ss.str());
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/**
 * see
 *   RIL_REQUEST_CDMA_SET_SUBSCRIPTION or
 *   RIL_REQUEST_CDMA_GET_SUBSCRIPTION in RIL
 */
void SimService::HandleCdmaSubscriptionSource(const Client& client,
                                              const std::string& command) {
  std::vector<std::string> responses;

  CommandParser cmd(command);
  cmd.SkipPrefix();
  if (*cmd == "AT+CCSS?") {  // Query
    std::stringstream ss;
    ss << "+CCSS: " << cdma_subscription_source_;
    responses.push_back(ss.str());
  } else { // Set
    cdma_subscription_source_ = cmd.GetNextInt();
  }
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/**
 * see
 *   RIL_REQUEST_CDMA_SET_ROAMNING_PREFERENCE or
 *   RIL_REQUEST_CDMA_GET_ROAMNING_PREFERENCE in RIL
 */
void SimService::HandleCdmaRoamingPreference(const Client& client,
                                             const std::string& command) {
  std::vector<std::string> responses;

  CommandParser cmd(command);
  cmd.SkipPrefix();
  if (*cmd == "AT+WRMP?") {  // Query
    std::stringstream ss;
    ss << "+WRMP: " << cdma_roaming_preference_;
    responses.push_back(ss.str());
  } else { // Set
    cdma_roaming_preference_ = cmd.GetNextInt();
  }
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

}  // namespace cuttlefish
