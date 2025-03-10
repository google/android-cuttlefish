/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <gflags/gflags.h>

#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/wmediumd_controller/wmediumd_controller.h"

const std::string usageMessage =
    "wmediumd control commandline utility\n\n"
    "  Usage: wmediumd_control [option] command [args...]\n\n"
    "  Commands:\n\n"
    "    set_snr mac1 mac2 snr\n"
    "      set SNR between two nodes. (0 <= snr <= 255)\n\n"
    "    reload_config [path]\n"
    "      force reload wmediumd configuration file\n\n"
    "      if path is not specified, reload current configuration file\n\n"
    "    start_pcap path\n"
    "      start packet capture and save capture result to file.\n"
    "      file format is pcap capture format.\n\n"
    "    stop_pcap\n"
    "      stop packet capture\n\n"
    "    list_stations\n"
    "      listing stations connected to wmediumd\n\n";

DEFINE_string(wmediumd_api_server, "",
              "Unix socket path of wmediumd api server");

const int kMacAddrStringSize = 17;

bool ValidMacAddr(const std::string& macAddr) {
  if (macAddr.size() != kMacAddrStringSize) {
    return false;
  }

  if (macAddr[2] != ':' || macAddr[5] != ':' || macAddr[8] != ':' ||
      macAddr[11] != ':' || macAddr[14] != ':') {
    return false;
  }

  for (int i = 0; i < kMacAddrStringSize; ++i) {
    if ((i - 2) % 3 == 0) continue;
    char c = macAddr[i];

    if (isupper(c)) {
      c = tolower(c);
    }

    if ((c < '0' || c > '9') && (c < 'a' || c > 'f')) return false;
  }

  return true;
}

std::string MacToString(const char* macAddr) {
  std::stringstream result;

  for (int i = 0; i < ETH_ALEN; i++) {
    result << std::setfill('0') << std::setw(2) << std::right << std::hex
           << static_cast<int>(static_cast<uint8_t>(macAddr[i]));

    if (i != 5) {
      result << ":";
    }
  }

  return result.str();
}

bool HandleSetSnrCommand(cuttlefish::WmediumdController& client,
                         const std::vector<std::string>& args) {
  if (args.size() != 4) {
    LOG(ERROR) << "error: set_snr must provide 3 options";
    return false;
  }

  if (!ValidMacAddr(args[1])) {
    LOG(ERROR) << "error: invalid mac address " << args[1];
    return false;
  }

  if (!ValidMacAddr(args[2])) {
    LOG(ERROR) << "error: invalid mac address " << args[2];
    return false;
  }

  uint8_t snr = 0;

  auto parseResult =
      android::base::ParseUint<decltype(snr)>(args[3].c_str(), &snr);

  if (!parseResult) {
    if (errno == EINVAL) {
      LOG(ERROR) << "error: cannot parse snr: " << args[3];
    } else if (errno == ERANGE) {
      LOG(ERROR) << "error: snr exceeded range: " << args[3];
    }

    return false;
  }

  if (!client.SetSnr(args[1], args[2], snr)) {
    return false;
  }

  return true;
}

bool HandleReloadConfigCommand(cuttlefish::WmediumdController& client,
                               const std::vector<std::string>& args) {
  if (args.size() > 2) {
    LOG(ERROR) << "error: reload_config must provide 0 or 1 option";
    return false;
  }

  if (args.size() == 2) {
    return client.ReloadConfig(args[1]);
  } else {
    return client.ReloadCurrentConfig();
  }
}

bool HandleStartPcapCommand(cuttlefish::WmediumdController& client,
                            const std::vector<std::string>& args) {
  if (args.size() != 2) {
    LOG(ERROR) << "error: you must provide only 1 option(path)";
    return false;
  }

  return client.StartPcap(args[1]);
}

bool HandleStopPcapCommand(cuttlefish::WmediumdController& client,
                           const std::vector<std::string>& args) {
  if (args.size() != 1) {
    LOG(ERROR) << "error: you must not provide option";
    return false;
  }

  return client.StopPcap();
}

bool HandleListStationsCommand(cuttlefish::WmediumdController& client,
                               const std::vector<std::string>& args) {
  if (args.size() != 1) {
    LOG(ERROR) << "error: you must not provide option";
    return false;
  }

  auto result = client.GetStations();

  if (!result) {
    LOG(ERROR) << "error: failed to get stations";
    return false;
  }

  auto stationList = result->GetStations();

  std::cout << "Total stations : " << stationList.size() << std::endl
            << std::endl;
  std::cout << "Mac Address      "
            << "\t"
            << "X Pos"
            << "\t"
            << "Y Pos"
            << "\t"
            << "TX Power" << std::endl;

  for (auto& station : stationList) {
    std::cout << MacToString(station.addr) << "\t" << std::setprecision(1)
              << std::fixed << station.x << "\t" << std::setprecision(1)
              << std::fixed << station.y << "\t" << station.tx_power
              << std::endl;
  }

  std::cout << std::endl;

  return true;
}

int main(int argc, char** argv) {
  gflags::SetUsageMessage(usageMessage);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::vector<std::string> args;

  for (int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }

  if (args.size() == 0) {
    LOG(ERROR) << "error: you must provide at least 1 argument";
    gflags::ShowUsageWithFlags(argv[0]);
    return -1;
  }

  std::string wmediumdApiServerPath(FLAGS_wmediumd_api_server);

  if (wmediumdApiServerPath == "") {
    const auto cuttlefishConfig = cuttlefish::CuttlefishConfig::Get();

    if (!cuttlefishConfig) {
      LOG(ERROR) << "error: cannot get global cuttlefish config";
      return -1;
    }

    wmediumdApiServerPath = cuttlefishConfig->wmediumd_api_server_socket();
  }

  auto client = cuttlefish::WmediumdController::New(wmediumdApiServerPath);

  if (!client) {
    LOG(ERROR) << "error: cannot connect to " << wmediumdApiServerPath;
    return -1;
  }

  auto commandMap =
      std::unordered_map<std::string,
                         std::function<bool(cuttlefish::WmediumdController&,
                                            const std::vector<std::string>&)>>{{
          {"set_snr", HandleSetSnrCommand},
          {"reload_config", HandleReloadConfigCommand},
          {"start_pcap", HandleStartPcapCommand},
          {"stop_pcap", HandleStopPcapCommand},
          {"list_stations", HandleListStationsCommand},
      }};

  if (commandMap.find(args[0]) == std::end(commandMap)) {
    LOG(ERROR) << "error: command " << args[0] << " does not exist";
    gflags::ShowUsageWithFlags(argv[0]);
    return -1;
  }

  if (!commandMap[args[0]](*client, args)) {
    LOG(ERROR) << "error: failed to execute command " << args[0];
    return -1;
  }

  return 0;
}
