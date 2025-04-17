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

#include "host/commands/modem_simulator/misc_service.h"

#include <fstream>
#include <iomanip>

#include "host/commands/modem_simulator/device_config.h"

namespace cuttlefish {

MiscService::MiscService(int32_t service_id, ChannelMonitor* channel_monitor,
                         ThreadLooper* thread_looper)
    : ModemService(service_id, this->InitializeCommandHandlers(),
                   channel_monitor, thread_looper) {
  ParseTimeZone();
}

void MiscService::ParseTimeZone() {
#if defined(__linux__)
  constexpr char TIMEZONE_FILENAME[] = "/etc/timezone";
  std::ifstream ifs = modem::DeviceConfig::open_ifstream_crossplat(TIMEZONE_FILENAME);
  if (ifs.is_open()) {
    std::string line;
    if (std::getline(ifs, line)) {
      FixTimeZone(line);
      timezone_ = line;
    }
  }
#endif
}

void MiscService::FixTimeZone(std::string& line) {
  auto slashpos = line.find("/");
  // "/" will be treated as separator, change it !
  if (slashpos != std::string::npos) {
    line.replace(slashpos, 1, "!");
  }
}

void MiscService::SetTimeZone(std::string timezone) {
  FixTimeZone(timezone);
  timezone_ = timezone;
}

std::vector<CommandHandler> MiscService::InitializeCommandHandlers() {
  std::vector<CommandHandler> command_handlers = {
      /* initializeCallback */
      CommandHandler("E0Q0V1",
                     [this](const Client& client) {
                       this->HandleCommandDefaultSupported(client);
                     }),
      CommandHandler("S0=0",
                     [this](const Client& client) {
                       this->HandleCommandDefaultSupported(client);
                     }),
      CommandHandler("+CMEE=1",
                     [this](const Client& client) {
                       this->HandleCommandDefaultSupported(client);
                     }),
      CommandHandler("+CMOD=0",
                     [this](const Client& client) {
                       this->HandleCommandDefaultSupported(client);
                     }),
      CommandHandler("+CSSN=0,1",
                     [this](const Client& client) {
                       this->HandleCommandDefaultSupported(client);
                     }),
      CommandHandler("+COLP=0",
                     [this](const Client& client) {
                       this->HandleCommandDefaultSupported(client);
                     }),
      CommandHandler("+CSCS=\"HEX\"",
                     [this](const Client& client) {
                       this->HandleCommandDefaultSupported(client);
                     }),
      CommandHandler("+CMGF=0",
                     [this](const Client& client) {
                       this->HandleCommandDefaultSupported(client);
                     }),

      CommandHandler("+CGSN",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleGetIMEI(client, cmd);
                     }),
      CommandHandler("+REMOTETIMEUPDATE",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleTimeUpdate(client, cmd);
                     }),
  };
  return (command_handlers);
}

void MiscService::HandleGetIMEI(const Client& client, std::string& command) {
  const std::string identityGsmImei = "867400022047199";
  const std::string identityGsmSvn = "01";
  const std::string information = "modem simulator";

  std::vector<std::string> responses;

  if (command == "AT+CGSN") {
    responses.push_back(identityGsmImei);
  } else {
    CommandParser cmd(command);
    cmd.SkipPrefix();
    int snt = cmd.GetNextInt();
    switch (snt) {
      case 0:  // SN: IMEI and more information provided by manufacturers
        responses.push_back(identityGsmImei + information);
        break;
      case 1:  // IMEI
        responses.push_back(identityGsmImei);
        break;
      case 2:  // IMEI and software version number
        responses.push_back(identityGsmImei + identityGsmSvn);
        break;
      case 3:  // Software version number
        responses.push_back(identityGsmSvn);
        break;
      default:  // Default IMEI
        responses.push_back(identityGsmImei);
        break;
    }
  }

  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

void MiscService::HandleTimeUpdate(const Client& client, std::string& command) {
    (void)client;
    (void)command;
    TimeUpdate();
}

long MiscService::TimeZoneOffset(time_t* utctime)
{
    struct tm local = *std::localtime(utctime);
    time_t local_time = std::mktime(&local);
    struct tm gmt = *std::gmtime(utctime);
    // mktime() converts struct tm according to local timezone.
    time_t gmt_time = std::mktime(&gmt);
    return (long)difftime(local_time, gmt_time);
}

void MiscService::TimeUpdate() {
  auto now = std::time(0);

  auto local_time = *std::localtime(&now);
  auto gm_time = *std::gmtime(&now);

  // Timezone offset is in number of quarter-hours
  auto tzdiff = TimeZoneOffset(&now) / (15 * 60);

  std::stringstream ss;
  ss << "%CTZV: " << std::setfill('0') << std::setw(2)
     << gm_time.tm_year % 100 << "/" << std::setfill('0') << std::setw(2)
     << gm_time.tm_mon + 1 << "/" << std::setfill('0') << std::setw(2)
     << gm_time.tm_mday << ":" << std::setfill('0') << std::setw(2)
     << gm_time.tm_hour << ":" << std::setfill('0') << std::setw(2)
     << gm_time.tm_min << ":" << std::setfill('0') << std::setw(2)
     << gm_time.tm_sec << (tzdiff >= 0 ? '+' : '-')
     << (tzdiff >= 0 ? tzdiff : -tzdiff) << ":" << local_time.tm_isdst;
  if (!timezone_.empty()) {
    ss << ":" << timezone_;
  }

  SendUnsolicitedCommand(ss.str());
}

}  // namespace cuttlefish
