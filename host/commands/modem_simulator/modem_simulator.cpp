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

#include <android-base/logging.h>

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

ModemSimulator::~ModemSimulator() {
  // this will stop the looper so all the callbacks
  // will be gone;
  thread_looper_->Stop();
  modem_services_.clear();
}

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
  auto networkservice = std::make_unique<NetworkService>(
      modem_id_, channel_monitor_.get(), thread_looper_.get());
  auto simservice = std::make_unique<SimService>(
      modem_id_, channel_monitor_.get(), thread_looper_.get());
  auto miscservice = std::make_unique<MiscService>(
      modem_id_, channel_monitor_.get(), thread_looper_.get());
  auto callservice = std::make_unique<CallService>(
      modem_id_, channel_monitor_.get(), thread_looper_.get());
  auto stkservice = std::make_unique<StkService>(
      modem_id_, channel_monitor_.get(), thread_looper_.get());
  auto smsservice = std::make_unique<SmsService>(
      modem_id_, channel_monitor_.get(), thread_looper_.get());
  auto dataservice = std::make_unique<DataService>(
      modem_id_, channel_monitor_.get(), thread_looper_.get());
  auto supservice = std::make_unique<SupService>(
      modem_id_, channel_monitor_.get(), thread_looper_.get());

  networkservice->SetupDependency(miscservice.get(), simservice.get(),
                                  dataservice.get());
  simservice->SetupDependency(networkservice.get());
  callservice->SetupDependency(simservice.get(), networkservice.get());
  stkservice->SetupDependency(simservice.get());
  smsservice->SetupDependency(simservice.get());

  sms_service_ = smsservice.get();
  sim_service_ = simservice.get();
  misc_service_ = miscservice.get();
  network_service_ = networkservice.get();
  modem_services_[kSimService] = std::move(simservice);
  modem_services_[kNetworkService] = std::move(networkservice);
  modem_services_[kCallService] = std::move(callservice);
  modem_services_[kDataService] = std::move(dataservice);
  modem_services_[kSmsService] = std::move(smsservice);
  modem_services_[kSupService] = std::move(supservice);
  modem_services_[kStkService] = std::move(stkservice);
  modem_services_[kMiscService] = std::move(miscservice);
}

void ModemSimulator::DispatchCommand(const Client& client, std::string& command) {
  if (sms_service_) {
    if (sms_service_->IsWaitingSmsPdu()) {
      sms_service_->HandleSendSMSPDU(client, command);
      return;
    } else if (sms_service_->IsWaitingSmsToSim()) {
      sms_service_->HandleWriteSMSPduToSim(client, command);
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
  if (misc_service_) {
    misc_service_->TimeUpdate();
  }

  if (network_service_) {
    network_service_->OnVoiceRegisterStateChanged();
    network_service_->OnDataRegisterStateChanged();
  }
}

void ModemSimulator::SaveModemState() {
  if (sim_service_) {
    sim_service_->SavePinStateToIccProfile();
    sim_service_->SaveFacilityLockToIccProfile();
  }
}

bool ModemSimulator::IsRadioOn() const {
  if (network_service_) {
    return !network_service_->isRadioOff();
  }
  return false;
}

bool ModemSimulator::IsWaitingSmsPdu() {
  if (sms_service_) {
    return (sms_service_->IsWaitingSmsPdu() ||
            sms_service_->IsWaitingSmsToSim());
  }
  return false;
}

void ModemSimulator::SetTimeZone(std::string timezone) {
  if (misc_service_) {
    misc_service_->SetTimeZone(timezone);
  }
}

bool ModemSimulator::SetPhoneNumber(std::string_view number) {
  if (sim_service_) {
    return sim_service_->SetPhoneNumber(number);
  }
  return false;
}

}  // namespace cuttlefish
