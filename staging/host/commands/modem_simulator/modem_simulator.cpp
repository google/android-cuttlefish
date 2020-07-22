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

#include "host/commands/modem_simulator/modem_simulator.h"

#include <memory>

#include "host/commands/modem_simulator/call_service.h"
#include "host/commands/modem_simulator/data_service.h"
#include "host/commands/modem_simulator/misc_service.h"
#include "host/commands/modem_simulator/network_service.h"
#include "host/commands/modem_simulator/sim_service.h"
#include "host/commands/modem_simulator/sms_service.h"
#include "host/commands/modem_simulator/stk_service.h"
#include "host/commands/modem_simulator/sup_service.h"

namespace cuttlefish {

ModemSimulator::ModemSimulator(int32_t modem_id)
    : modem_id_(modem_id), thread_looper_(new ThreadLooper()) {}

void ModemSimulator::LoadNvramConfig() {
  auto nvram_config = NvramConfig::Get();
  if (!nvram_config) {
    LOG(FATAL) << "Failed to obtain nvram config singleton";
    return;
  }
}

void ModemSimulator::Initialize(
    std::unique_ptr<ChannelMonitor>&& channel_monitor) {
  channel_monitor_ = std::move(channel_monitor);
  LoadNvramConfig();
  RegisterModemService();
}

void ModemSimulator::RegisterModemService() {
  auto netservice = std::make_unique<NetworkService>(
      modem_id_, channel_monitor_.get(), thread_looper_);
  auto simservice = std::make_unique<SimService>(
      modem_id_, channel_monitor_.get(), thread_looper_);
  auto miscservice = std::make_unique<MiscService>(
      modem_id_, channel_monitor_.get(), thread_looper_);
  auto callservice = std::make_unique<CallService>(
      modem_id_, channel_monitor_.get(), thread_looper_);
  auto stkservice = std::make_unique<StkService>(
      modem_id_, channel_monitor_.get(), thread_looper_);
  auto smsservice = std::make_unique<SmsService>(
      modem_id_, channel_monitor_.get(), thread_looper_);
  auto dataservice = std::make_unique<DataService>(
      modem_id_, channel_monitor_.get(), thread_looper_);
  auto supservice = std::make_unique<SupService>(
      modem_id_, channel_monitor_.get(), thread_looper_);

  netservice->SetupDependency(miscservice.get(), simservice.get(),
                              dataservice.get());
  simservice->SetupDependency(netservice.get());
  callservice->SetupDependency(simservice.get(), netservice.get());
  stkservice->SetupDependency(simservice.get());
  smsservice->SetupDependency(simservice.get());

  modem_services_[kSimService] = std::move(simservice);
  modem_services_[kNetworkService] = std::move(netservice);
  modem_services_[kCallService] = std::move(callservice);
  modem_services_[kDataService] = std::move(dataservice);
  modem_services_[kSmsService] = std::move(smsservice);
  modem_services_[kSupService] = std::move(supservice);
  modem_services_[kStkService] = std::move(stkservice);
  modem_services_[kMiscService] = std::move(miscservice);
}

void ModemSimulator::DispatchCommand(const Client& client, std::string& command) {
  auto iter = modem_services_.find(kSmsService);
  if (iter != modem_services_.end()) {
    auto sms_service =
        dynamic_cast<SmsService*>(modem_services_[kSmsService].get());
    if (sms_service->IsWaitingSmsPdu()) {
      sms_service->HandleSendSMSPDU(client, command);
      return;
    } else if (sms_service->IsWaitingSmsToSim()) {
      sms_service->HandleWriteSMSPduToSim(client, command);
      return;
    }
  }

  bool success = false;
  for (auto& service : modem_services_) {
    success = service.second->HandleModemCommand(client, command);
    if (success) {
      break;
    }
  }

  if (!success && client.type != Client::REMOTE) {
    LOG(DEBUG) << "Not supported AT command: " << command;
    client.SendCommandResponse(ModemService::kCmeErrorOperationNotSupported);
  }
}

void ModemSimulator::OnFirstClientConnected() {
  auto iter = modem_services_.find(kMiscService);
  if (iter != modem_services_.end()) {
    auto misc_service =
        dynamic_cast<MiscService*>(modem_services_[kMiscService].get());
    misc_service->TimeUpdate();
  }

  iter = modem_services_.find(kNetworkService);
  if (iter != modem_services_.end()) {
    auto network_service =
        dynamic_cast<NetworkService*>(modem_services_[kNetworkService].get());
    network_service->OnVoiceRegisterStateChanged();
    network_service->OnDataRegisterStateChanged();
  }
}

void ModemSimulator::SaveModemState() {
  auto iter = modem_services_.find(kSimService);
  if (iter != modem_services_.end()) {
    auto sim_service =
        dynamic_cast<SimService*>(modem_services_[kSimService].get());
    sim_service->SavePinStateToIccProfile();
    sim_service->SaveFacilityLockToIccProfile();
  }
}

bool ModemSimulator::IsWaitingSmsPdu() {
  auto iter = modem_services_.find(kSmsService);
  if (iter != modem_services_.end()) {
    auto sms_service =
        dynamic_cast<SmsService*>(modem_services_[kSmsService].get());
    return (sms_service->IsWaitingSmsPdu() | sms_service->IsWaitingSmsToSim());
  }
  return false;
}

}  // namespace cuttlefish
