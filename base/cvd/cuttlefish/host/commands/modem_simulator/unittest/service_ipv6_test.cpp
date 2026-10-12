//
// Copyright (C) 2026 The Android Open Source Project
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

// End-to-end AT tests of the modem simulator with mobile IPv6 configured:
// CuttlefishConfig ril_ipv6_* -> DeviceConfig -> DataService -> +CGCONTRDP.
// service_test.cpp covers the same commands without IPv6. This is a separate
// binary because the config is loaded once per process.

#include <gtest/gtest.h>
#include <stdlib.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "iccfile.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/modem_simulator/channel_monitor.h"
#include "cuttlefish/host/commands/modem_simulator/device_config.h"
#include "cuttlefish/host/commands/modem_simulator/modem_simulator.h"
#include "cuttlefish/host/commands/modem_simulator/nvram_config.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

namespace fs = std::filesystem;

const std::string kTestDir =
    std::string(fs::temp_directory_path()) + "/cuttlefish_modem_ipv6_test";

class ModemIpv6ServiceTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    CuttlefishConfig config;
    const std::string config_file = kTestDir + "/.cuttlefish_config.json";
    config.set_root_dir(kTestDir + "/cuttlefish");
    auto instance = config.ForInstance(GetInstance());
    instance.set_ril_ipaddr("192.168.97.2");
    instance.set_ril_gateway("192.168.97.1");
    instance.set_ril_prefixlen(30);
    instance.set_ril_dns("8.8.8.8");
    instance.set_ril_ipv6_ipaddr("fd00:cf:21:1::2");
    instance.set_ril_ipv6_gateway("fd00:cf:21:1::1");
    instance.set_ril_ipv6_dns("2001:4860:4860::8888");
    instance.set_ril_ipv6_prefixlen(64);
    for (const auto& inst : config.Instances()) {
      fs::create_directories(inst.instance_dir());
      ASSERT_TRUE(
          config.SaveToFile(inst.PerInstancePath("cuttlefish_config.json")));
      std::ofstream icc(inst.PerInstancePath("/iccprofile_for_sim0.xml"));
      icc << std::string(myiccfile);
      icc.close();
      fs::copy_file(inst.PerInstancePath("cuttlefish_config.json"), config_file,
                    fs::copy_options::overwrite_existing);
    }
    ::setenv("CUTTLEFISH_CONFIG_FILE", config_file.c_str(), 1);

    SharedFD ril_fd, modem_fd;
    ASSERT_TRUE(
        SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0, &ril_fd, &modem_fd));
    NvramConfig::InitNvramConfigService(1, 1);
    ril_fd_ = new SharedFD(ril_fd);
    modem_side_ = new Client(modem_fd);
    modem_simulator_ = new ModemSimulator(0);
    SharedFD server;
    modem_simulator_->Initialize(
        std::make_unique<ChannelMonitor>(*modem_simulator_, server));
  }

  static void TearDownTestSuite() {
    delete ril_fd_;
    delete modem_side_;
    delete modem_simulator_;
    fs::remove_all(kTestDir);
  }

  // Sends command and returns the lines starting with prefix, followed by
  // the final result line (OK or an error).
  std::vector<std::string> Send(const std::string& command,
                                const std::string& prefix) {
    std::string mutable_command = command;
    modem_simulator_->DispatchCommand(*modem_side_, mutable_command);
    std::vector<std::string> lines;
    std::string pending;
    while (true) {
      std::vector<char> buf(4096);
      Result<uint64_t> n = (*ril_fd_)->Read(buf.data(), buf.size() - 1);
      if (n.value_or(0) == 0) {
        ADD_FAILURE() << "modem closed while reading response to " << command;
        return lines;
      }
      pending.append(buf.data(), *n);
      size_t pos;
      while ((pos = pending.find_first_of("\r\n")) != std::string::npos) {
        std::string line = pending.substr(0, pos);
        pending.erase(0, pos + 1);
        if (line.empty()) {
          continue;
        }
        if (line == "OK" || line.rfind("ERROR", 0) == 0 ||
            line.rfind("+CME ERROR", 0) == 0) {
          lines.push_back(line);
          return lines;
        }
        if (!prefix.empty() && line.rfind(prefix, 0) == 0) {
          lines.push_back(line);
        }
      }
    }
  }

  // The RIL end of the socket pair; responses to modem_side_ arrive here.
  static SharedFD* ril_fd_;
  static Client* modem_side_;
  static ModemSimulator* modem_simulator_;
};

SharedFD* ModemIpv6ServiceTest::ril_fd_ = nullptr;
Client* ModemIpv6ServiceTest::modem_side_ = nullptr;
ModemSimulator* ModemIpv6ServiceTest::modem_simulator_ = nullptr;

TEST_F(ModemIpv6ServiceTest, DeviceConfigReadsIpv6FromCuttlefishConfig) {
  EXPECT_EQ(modem::DeviceConfig::ril_ipv6_address_and_prefix(),
            "fd00:cf:21:1::2/64");
  EXPECT_EQ(modem::DeviceConfig::ril_ipv6_gateway(), "fd00:cf:21:1::1");
  EXPECT_EQ(modem::DeviceConfig::ril_ipv6_dns(), "2001:4860:4860::8888");
  EXPECT_EQ(modem::DeviceConfig::ril_address_and_prefix(), "192.168.97.2/30");
}

// Every PDP type gets the IPv4 line first and the IPv6 line second, and
// +CGDCONT? keeps reporting the IPv4 address.
TEST_F(ModemIpv6ServiceTest, DualStackDynamicParamsPerPdpType) {
  int cid = 21;
  for (const char* pdp_type : {"IP", "IPV6", "IPV4V6"}) {
    const std::string c = std::to_string(cid);
    EXPECT_EQ(
        Send("AT+CGDCONT=" + c + ",\"" + pdp_type + "\",\"ctlte\",,0,0", ""),
        std::vector<std::string>{"OK"})
        << pdp_type;
    EXPECT_EQ(Send("AT+CGCONTRDP=" + c, "+CGCONTRDP:"),
              (std::vector<std::string>{
                  "+CGCONTRDP: " + c +
                      ",5,\"ctlte\",192.168.97.2/30,192.168.97.1,8.8.8.8",
                  "+CGCONTRDP: " + c +
                      ",5,\"ctlte\",fd00:cf:21:1::2/64,fd00:cf:21:1::1,"
                      "2001:4860:4860::8888",
                  "OK"}))
        << pdp_type;
    std::vector<std::string> list = Send("AT+CGDCONT?", "+CGDCONT: " + c + ",");
    EXPECT_EQ(list,
              (std::vector<std::string>{"+CGDCONT: " + c + ",\"" + pdp_type +
                                            "\",\"ctlte\","
                                            "192.168.97.2/30,0,0",
                                        "OK"}))
        << pdp_type;
    ++cid;
  }
}

// Redefining a cid replaces its parameters (no stale second line).
TEST_F(ModemIpv6ServiceTest, RedefineCidKeepsTwoLines) {
  ASSERT_EQ(Send("AT+CGDCONT=30,\"IP\",\"ctlte\",,0,0", ""),
            std::vector<std::string>{"OK"});
  ASSERT_EQ(Send("AT+CGDCONT=30,\"IPV4V6\",\"ims\",,0,0", ""),
            std::vector<std::string>{"OK"});
  std::vector<std::string> lines = Send("AT+CGCONTRDP=30", "+CGCONTRDP:");
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0].rfind("+CGCONTRDP: 30,5,\"ims\",192.168.97.2/30", 0), 0u);
  EXPECT_EQ(lines[1].rfind("+CGCONTRDP: 30,5,\"ims\",fd00:cf:21:1::2/64", 0),
            0u);
}

TEST_F(ModemIpv6ServiceTest, UnknownCidIsError) {
  EXPECT_EQ(Send("AT+CGCONTRDP=99", "+CGCONTRDP:"),
            std::vector<std::string>{"+CME ERROR: 21"});
}

}  // namespace
}  // namespace cuttlefish
