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

#include <android-base/logging.h>
#include <gtest/gtest.h>
#include <stdlib.h>

#include <filesystem>
#include <fstream>

#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/files.h"
#include "host/libs/config/cuttlefish_config.h"
#include "modem_simulator/channel_monitor.h"
#include "modem_simulator/modem_simulator.h"
namespace fs = std::filesystem;

static const char *myiccfile =
#include "iccfile.txt"
    ;

static const std::string tmp_test_dir = std::string(fs::temp_directory_path()) +
                                        std::string("/cuttlefish_modem_test");
using namespace cuttlefish;

class ModemServiceTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    {
      cuttlefish::CuttlefishConfig tmp_config_obj;
      std::string config_file = tmp_test_dir + "/.cuttlefish_config.json";
      std::string instance_dir = tmp_test_dir + "/cuttlefish_runtime.1";
      fs::create_directories(instance_dir);
      tmp_config_obj.set_ril_dns("8.8.8.8");
      std::vector<int> instance_nums;
      for (int i = 0; i < 1; i++) {
        instance_nums.push_back(cuttlefish::GetInstance() + i);
      }
      for (const auto &num : instance_nums) {
        auto instance = tmp_config_obj.ForInstance(num);
        instance.set_instance_dir(instance_dir);
      }

      for (auto instance : tmp_config_obj.Instances()) {
        if (!tmp_config_obj.SaveToFile(
                instance.PerInstancePath("cuttlefish_config.json"))) {
          LOG(ERROR) << "Unable to save copy config object";
          return;
        }
      }
      fs::copy_file(instance_dir + "/cuttlefish_config.json", config_file,
                    fs::copy_options::overwrite_existing);
      std::string icfilename = instance_dir + "/iccprofile_for_sim0.xml";
      std::ofstream offile(icfilename, std::ofstream::out);
      offile << std::string(myiccfile);
      offile.close();

      ::setenv("CUTTLEFISH_CONFIG_FILE", config_file.c_str(), 1);
    }
    cuttlefish::SharedFD ril_shared_fd, modem_shared_fd;
    if (!SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0, &ril_shared_fd,
                              &modem_shared_fd)) {
      LOG(ERROR) << "Unable to create client socket pair: " << strerror(errno);
    }
    ASSERT_TRUE(ril_shared_fd->IsOpen());
    ASSERT_TRUE(modem_shared_fd->IsOpen());

    NvramConfig::InitNvramConfigService(1);

    ril_side_ = new Client(ril_shared_fd);
    modem_side_ = new Client(modem_shared_fd);
    modem_simulator_ = new ModemSimulator(0);

    cuttlefish::SharedFD server;
    auto channel_monitor =
        std::make_unique<ChannelMonitor>(modem_simulator_, server);
    modem_simulator_->Initialize(std::move(channel_monitor));
  }

  static void TearDownTestSuite() {
    delete ril_side_;
    delete modem_side_;
    delete modem_simulator_;
    fs::remove_all(tmp_test_dir);
  };

  void SendCommand(std::string command, std::string prefix = "") {
    command_prefix_ = prefix;
    modem_simulator_->DispatchCommand(*modem_side_, command);
  }

  void ReadCommandResponse(std::vector<std::string>& response) {
    do {
      std::vector<char> buffer(4096);  // kMaxCommandLength
      auto bytes_read = ril_side_->client_fd->Read(buffer.data(), buffer.size());
      if (bytes_read <= 0) {
        // Close here to ensure the other side gets reset if it's still
        // connected
        ril_side_->client_fd->Close();
        LOG(WARNING) << "Detected close from the other side";
        break;
      }

      std::string& incomplete_command = ril_side_->incomplete_command;

      // Add the incomplete command from the last read
      auto commands = std::string{incomplete_command.data()};
      commands.append(buffer.data());

      incomplete_command.clear();
      incomplete_command.resize(0);

      // replacing '\n' with '\r'
      commands = android::base::StringReplace(commands, "\n", "\r", true);

      // split into commands and dispatch
      size_t pos = 0, r_pos = 0;  // '\r' or '\n'
      while (r_pos != std::string::npos) {
        r_pos = commands.find('\r', pos);
        if (r_pos != std::string::npos) {
          auto command = commands.substr(pos, r_pos - pos);
          if (command.size() > 0) {  // "\r\r" ?
            LOG(DEBUG) << "AT< " << command;
            if (IsFinalResponseSuccess(command) || IsFinalResponseError(command)) {
              response.push_back(command);
              return;
            } else if (IsIntermediateResponse(command)) {
              response.push_back(command);
            } else {
              ; // Ignore unsolicited command
            }
          }
          pos = r_pos + 1;  // skip '\r'
        } else if (pos < commands.length()) {  // incomplete command
          incomplete_command = commands.substr(pos);
          LOG(VERBOSE) << "incomplete command: " << incomplete_command;
        }
      }
    } while (true);

    // read response
  }

  inline bool IsFinalResponseSuccess(std::string response) {
    auto iter = kFinalResponseSuccess.begin();
    for (; iter != kFinalResponseSuccess.end(); ++iter) {
      if (*iter == response) {
        return true;
      }
    }

    return false;
  }

  inline bool IsFinalResponseError(std::string response) {
    auto iter = kFinalResponseError.begin();
    for (; iter != kFinalResponseError.end(); ++iter) {
      if (response.compare(0, iter->size(), *iter) == 0) {
        return true;
      }
    }

    return false;
  }

  inline bool IsIntermediateResponse(std::string response) {
    if (response.compare(0, command_prefix_.size(), command_prefix_) == 0) {
      return true;
    }

    return false;
  }

  int openLogicalChannel(std::string& name) {
    // Create and send command
    std::string command = "AT+CCHO=";
    command.append(name);
    std::vector<std::string> response;
    SendCommand(command);
    ReadCommandResponse(response);
    int channel = std::stoi(response[0]);
    return channel;
  }

  bool closeLogicalChannel(int channel) {
    std::string command = "AT+CCHC=";
    command += std::to_string(channel);
    std::vector<std::string> response;
    SendCommand(command);
    ReadCommandResponse(response);
    std::string expect = "+CCHC";
    return (response[0].compare(0, expect.size(), expect) == 0);
  }

  const std::vector<std::string> kFinalResponseSuccess = {"OK", "CONNECT", "> "};
  const std::vector<std::string> kFinalResponseError = {
      "ERROR",
      "+CMS ERROR:",
      "+CME ERROR:",
      "NO CARRIER", /* sometimes! */
      "NO ANSWER",
      "NO DIALTONE",
  };

  static Client* ril_side_;
  static Client* modem_side_;
  static ModemSimulator* modem_simulator_;

  // For distinguishing the response from command response or unsolicited command
  std::string command_prefix_;
};

ModemSimulator* ModemServiceTest::modem_simulator_ = nullptr;
Client* ModemServiceTest::ril_side_ = nullptr;
Client* ModemServiceTest::modem_side_ = nullptr;

/* Sim Service Test */
TEST_F(ModemServiceTest, GetIccCardStatus) {
  const char *expects[]  = {"+CPIN: READY",
                            "OK"};

  std::string command = "AT+CPIN?";
  std::vector<std::string> response;
  SendCommand(command, "+CPIN:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  ASSERT_STREQ(response[0].c_str(), expects[0]);
  ASSERT_STREQ(response[1].c_str(), expects[1]);
}

TEST_F(ModemServiceTest, ChangeOrEnterPIN) {
  std::vector<std::string> commands = {"AT+CPIN=1234,0000",
                                       "AT+CPIN=1111,2222",};
  std::vector<std::string> expects  = {"OK",
                                       "+CME ERROR: 16",};
  std::vector<std::string> response;
  auto expects_iter = expects.begin();
  for (auto iter = commands.begin(); iter != commands.end(); ++iter, ++expects_iter) {
    SendCommand(*iter);
    ReadCommandResponse(response);
    ASSERT_STREQ(response[0].c_str(), (*expects_iter).c_str());
    response.clear();
  }
}

TEST_F(ModemServiceTest, SIM_IO) {
  std::vector<std::string> commands = {"AT+CRSM=192,12258,0,0,15",
                                       "AT+CRSM=192,28436,0,0,15",
                                       "AT+CRSM=220,28618,1,4,5,0000000000"};
  std::vector<std::string> expects  = {"+CRSM: 144,0,62178202412183022FE28A01058B032F06038002000A880110",
                                       "+CRSM: 106,130",
                                       "+CRSM: 144,0"};

  std::vector<std::string> response;
  auto expects_iter = expects.begin();
  for (auto iter = commands.begin(); iter != commands.end(); ++iter, ++expects_iter) {
    SendCommand(*iter);
    ReadCommandResponse(response);
    ASSERT_EQ(response.size(), 2);
    ASSERT_STREQ(response[0].c_str(), (*expects_iter).c_str());
    response.clear();
  }
}

TEST_F(ModemServiceTest, GetIMSI) {
  std::string command = "AT+CIMI";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  const char *expect = "460110031689666";
  ASSERT_STREQ(response[0].c_str(),expect);
}

TEST_F(ModemServiceTest, GetIccId) {
  std::string command = "AT+CICCID";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  const char *expect = "89860318640220133897";
  ASSERT_STREQ(response[0].c_str(),expect);
}

TEST_F(ModemServiceTest, FacilityLock) {
  std::vector<std::string> commands =
        { "AT+CLCK=\"FD\",2,"",7",
          "AT+CLCK=\"SC\",2,"",7",
          "AT+CLCK=\"SC\",1,\"1234\",7",
          "AT+CLCK=\"SC\",1,\"023000\",7"
  };
  std::vector<std::string> expects =
        { "+CLCK: 0",
          "+CLCK: 0",
          "+CME ERROR: 16",
          "+CME ERROR: 16"
  };
  std::vector<std::string> response;
  auto expects_iter = expects.begin();
  for (auto iter = commands.begin(); iter != commands.end(); ++iter, ++expects_iter) {
    SendCommand(*iter);
    ReadCommandResponse(response);
    ASSERT_STREQ(response[0].c_str(), (*expects_iter).c_str());
    response.clear();
  }
}

TEST_F(ModemServiceTest, OpenLogicalChannel) {
  std::string command= "A00000015141434C00";
  int firstChannel = openLogicalChannel(command);
  ASSERT_EQ(firstChannel, 1);

  command= "A00000015144414300";
  int secondChannel = openLogicalChannel(command);
  ASSERT_GE(secondChannel, 1);

  closeLogicalChannel(firstChannel);
  closeLogicalChannel(secondChannel);
}

TEST_F(ModemServiceTest, CloseLogicalChannel) {
  std::string command= "A00000015141434C00";
  int channel = openLogicalChannel(command);
  ASSERT_EQ(channel, 1);

  ASSERT_FALSE(closeLogicalChannel(channel + 3));
  ASSERT_TRUE(closeLogicalChannel(channel));
}

TEST_F(ModemServiceTest, TransmitLogicalChannel) {
  std::string command= "A00000015144414300";
  int channel = openLogicalChannel(command);
  ASSERT_EQ(channel, 1);
  command = "AT+CGLA=";
  command += channel;
  command += ",10,80caff4000";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  const char *expect = "+CME ERROR: 21";
  ASSERT_STREQ(response[0].c_str(),expect);
  ASSERT_TRUE(closeLogicalChannel(channel));
}

/* Network Service Test */
TEST_F(ModemServiceTest, testRadioPowerReq) {
  std::string command = "AT+CFUN?";
  std::vector<std::string> response;
  SendCommand(command, "+CFUN:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, testSetRadioPower) {
  std::string command = "AT+CFUN=1";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 1);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, testSignalStrength) {
  std::string command = "AT+CSQ";
  std::vector<std::string> response;
  SendCommand(command, "+CSQ:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, testQueryNetworkSelectionMode) {
  std::string command = "AT+COPS?";
  std::vector<std::string> response;
  SendCommand(command, "+COPS:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, testRequestOperator) {
  std::string command = "AT+COPS=3,0;+COPS?;+COPS=3,1;+COPS?;+COPS=3,2;+COPS?";
  std::vector<std::string> response;
  SendCommand(command, "+COPS:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 4);
}

TEST_F(ModemServiceTest, testVoiceNetworkRegistration) {
  std::string command = "AT+CREG?";
  std::vector<std::string> response;
  SendCommand(command, "+CREG:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, testDataNetworkRegistration) {
  std::string command = "AT+CGREG?";
  std::vector<std::string> response;
  SendCommand(command, "+CGREG:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, testDataNetworkRegistrationWithLte2) {
  std::string command = "AT+CEREG?";
  std::vector<std::string> response;
  SendCommand(command, "+CEREG:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, testGetPreferredNetworkType) {
  std::string command = "AT+CTEC?";
  std::vector<std::string> response;
  SendCommand(command, "+CTEC:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, testQuerySupportedTechs) {
  std::string command = "AT+CTEC=?";
  std::vector<std::string> response;
  SendCommand(command, "+CTEC:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, testSetPreferredNetworkType) {
  std::string command = "AT+CTEC=1,\"201\"";
  std::vector<std::string> response;
  SendCommand(command, "+CTEC:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

/* Call Service Test */
TEST_F(ModemServiceTest, testCurrentCalls) {
  std::string command = "AT+CLCC";
  std::vector<std::string> response;
  SendCommand(command, "+CLCC:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 1);
}

TEST_F(ModemServiceTest, testHangup) {
  for (int i = 0; i < 5; i ++) {
    std::stringstream ss;
    ss.clear();
    ss << "AT+CHLD=" << i;
    SendCommand(ss.str());
  }
  std::vector<std::string> response;
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 1);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, testMute) {
  std::string command = "AT+CMUT=1";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 1);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, testSendDtmf) {
  std::string command = "AT+VTS=1";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 1);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, testExitEmergencyMode) {
  std::string command = "AT+WSOS=0";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 1);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

/* Data Service Test */
TEST_F(ModemServiceTest, SetPDPContext) {
  std::string command = "AT+CGDCONT=1,\"IPV4V6\",\"ctlte\",,0,0";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 1);
  ASSERT_STREQ(response.at(0).c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, QueryPDPContextList) {
  for (int i = 1; i < 5; i ++) {
    std::stringstream ss;
    ss.clear();
    ss << "AT+CGDCONT=" << i << ",\"IPV4V6\",\"ctlte\",,0,0";
    SendCommand(ss.str());
  }
  std::string command = "AT+CGDCONT?";
  std::vector<std::string> response;
  SendCommand(command, "+CGDCONT:");
  ReadCommandResponse(response);
  const char *result = response[response.size() - 1].c_str();
  ASSERT_EQ(response.size(), 1);
  ASSERT_STREQ(result, kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, ActivateDataCall) {
  std::string command = "AT+CGACT= 1,0";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  const char *result = response[0].c_str();
  ASSERT_STREQ(result, kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, QueryDataCallList) {
  std::string command = "AT+CGACT?";
  std::vector<std::string> response;
  SendCommand(command, "+CGACT:");
  ReadCommandResponse(response);
  const char *result = response[response.size() - 1].c_str();
  ASSERT_STREQ(result, kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, ReadDynamicParamTrue) {
  std::string command = "AT+CGCONTRDP=1";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  const char *result = response[response.size() - 1].c_str();
  ASSERT_STREQ(result, kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, ReadDynamicParamFalse) {
  std::string command = "AT+CGCONTRDP=10";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  const char *result = response[response.size() - 1].c_str();
  const char *expect = "+CME ERROR: 21";
  ASSERT_STREQ(result, expect);
}

TEST_F(ModemServiceTest, EnterDataState) {
  std::string command = "AT+CGDATA=1,1";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  const char *result = response[response.size() - 1].c_str();
  ASSERT_STREQ(result, kFinalResponseSuccess[1].c_str());
}

/* SMS Service Test */
TEST_F(ModemServiceTest, SendSMS) {
  std::string command = "AT+CMGS=35";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  const char *result = response[response.size() - 1].c_str();
  const char *expect = "> ";
  ASSERT_STREQ(result, expect);
  command = "0001000D91688118109844F0000017AFD7903AB55A9BBA69D639D4ADCBF99E3DCCAE9701^Z";
  //command += '\032';
  SendCommand(command);
  ReadCommandResponse(response);
  // TODO (bohu) for some reason the follwoing asserts fail, fix them
  // ASSERT_EQ(response.size(), 3);
  // ASSERT_STREQ(response[response.size() - 1].c_str(),
  // kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, WriteSMSToSim) {
  std::string command = "AT+CMGW=24,3";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  const char *result = response[response.size() - 1].c_str();
  const char *expect = "> ";
  ASSERT_STREQ(result, expect);
  command = "00240B815123106351F100000240516054410005C8329BFD06^Z";
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 3);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, SMSAcknowledge) {
  std::string command = "AT+CNMA=1";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  const char *result = response[response.size() - 1].c_str();
  ASSERT_STREQ(result, kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, DeleteSmsOnSimTure) {
  std::string command = "AT+CMGD=1";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  const char *result = response[0].c_str();
  ASSERT_STREQ(result, kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, DeleteSmsOnSimFalse) {
  std::string command = "AT+CMGD=1";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  const char *result = response[0].c_str();
  const char *expect = "+CME ERROR: 21";
  ASSERT_STREQ(result, expect);
}

TEST_F(ModemServiceTest, SetBroadcastConfig) {
  std::string command = "AT+CSCB=0,\"4356\",\"0-255\"";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  const char *result = response[0].c_str();
  ASSERT_STREQ(result, kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, GetBroadcastConfig) {
  std::string command = "AT+CSCB?";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  const char *result = response[response.size() - 1].c_str();
  ASSERT_STREQ(result, kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, SetSmscAddress) {
  std::string command = "AT+CSCA=\"91688115667566F4\",16";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 1);
  const char *result = response[response.size() - 1].c_str();
  ASSERT_STREQ(result, kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, GetSmscAddress) {
  std::string command = "AT+CSCA?";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  const char *result = response[response.size() - 1].c_str();
  ASSERT_STREQ(result, kFinalResponseSuccess[0].c_str());
}

/* SUP Service Test */
TEST_F(ModemServiceTest, testUSSD) {
  std::string command = "AT+CUSD=1";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 1);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, testCLIR) {
  std::string command = "AT+CLIR=2";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 1);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, testQueryCLIR) {
  std::string command = "AT+CLIR?";
  std::vector<std::string> response;
  SendCommand(command, "+CLIR:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
}

TEST_F(ModemServiceTest, testCallWaiting) {
  std::string command = "AT+CCWA";
  std::vector<std::string> response;
  SendCommand(command, "+CCWA:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 1);
  ASSERT_STREQ(response[response.size() - 1].c_str(), kFinalResponseSuccess[0].c_str());
}

TEST_F(ModemServiceTest, testCLIP) {
  std::string command = "AT+CLIP?";
  std::vector<std::string> response;
  SendCommand(command, "+CLIP:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
}

TEST_F(ModemServiceTest, testCallForward) {
  std::string command = "AT+CCFCU=1,1,2,145,\"10086\"";
  std::vector<std::string> response;
  SendCommand(command, "+CCFCU:");
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 1);
}

/* STK Service Test */
TEST_F(ModemServiceTest, ReportStkServiceIsRunning) {
  std::string command = "AT+CUSATD?";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  const char *result = response[0].c_str();
  const char *expect = "+CUSATD: 0,1";
  ASSERT_STREQ(result, expect);
}

TEST_F(ModemServiceTest, SendEnvelope) {
  std::string command = "AT+CUSATT=\"810301250002028281830100\"";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  const char *result = response[0].c_str();
  const char *expect = "+CUSATT: 0";
  ASSERT_STREQ(result, expect);
}

TEST_F(ModemServiceTest, GetSendTerminalResponseToSim) {
  std::string command = "AT+CUSATE=\"D3078202018190014E\"";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  const char *result = response[0].c_str();
  const char *expect = "+CUSATE: 0";
  ASSERT_STREQ(result, expect);
}

/* Misc Service Test */
TEST_F(ModemServiceTest, GetIMEI) {
  std::string command = "AT+CGSN";
  std::vector<std::string> response;
  SendCommand(command);
  ReadCommandResponse(response);
  ASSERT_EQ(response.size(), 2);
  const char *result = response[0].c_str();
  const char *expect = "12345678902468";
  ASSERT_STREQ(result, expect);
}
