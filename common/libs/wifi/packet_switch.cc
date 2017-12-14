/*
 * Copyright (C) 2017 The Android Open Source Project
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
#include "common/libs/wifi/packet_switch.h"

#include "common/libs/wifi/mac80211.h"
#include "common/libs/wifi/router.h"

#ifdef CUTTLEFISH_HOST
#include "host/libs/config/host_config.h"
#endif

namespace cvd {

PacketSwitch::~PacketSwitch() { Stop(); }

bool PacketSwitch::Init() {
  bool res;
#ifdef CUTTLEFISH_HOST
  res = shm_wifi_.Open(vsoc::GetDomain().c_str());
#else
  res = shm_wifi_.Open();
#endif

  if (res) {
    worker_ = shm_wifi_.StartWorker();
  }
  return res;
}

void PacketSwitch::Start() {
  std::lock_guard<std::mutex> l(op_mutex_);
  if (started_) return;
  // set started to true immediately; this attribute is referenced by threads to
  // know whether they should terminate.
  started_ = true;

  nl_->WRCL().SetDefaultHandler(
      [this](nl_msg* m) { ProcessPacket(m, false); });

  shm_xchg_.reset(new std::thread([this] {
      size_t maxlen = getpagesize();
      std::unique_ptr<uint8_t[]> msg(new uint8_t[maxlen]);
      auto hdr = reinterpret_cast<nlmsghdr*>(msg.get());
      std::unique_ptr<nl_msg, void (*)(nl_msg*)> nlm(nullptr, nlmsg_free);

      while (started_) {
#ifdef CUTTLEFISH_HOST
      LOG(INFO) << "Awaiting packet.";
#endif
        auto len = shm_wifi_.Recv(msg.get(), maxlen);
#ifdef CUTTLEFISH_HOST
      LOG(INFO) << "Received packet.";
#endif
        nlm.reset(nlmsg_convert(hdr));
        ProcessPacket(nlm.get(), true);
      }
    }));
}

void PacketSwitch::Stop() {
  std::lock_guard<std::mutex> l(op_mutex_);
  if (!started_) return;
  started_ = false;
  nl_->WRCL().SetDefaultHandler(std::function<void(nl_msg*)>());

  shm_xchg_->join();
  shm_xchg_.reset();
}

void PacketSwitch::ProcessPacket(nl_msg* m, bool is_incoming) {
  auto header = nlmsg_hdr(m);
  auto genhdr = reinterpret_cast<genlmsghdr*>(nlmsg_data(header));

  if (genhdr->cmd == WIFIROUTER_CMD_NOTIFY) {
    // If origin is not local (= not set from local WIFI), then forward it to
    // local WIFI.
    if (is_incoming) {
#ifdef CUTTLEFISH_HOST
      LOG(INFO) << "Forwarding packet.";
#endif
      // Need to update MAC80211_HWSIM WIFI family before injecting packet.
      // Different kernels may have different family numbers allocated.
      header->nlmsg_type = nl_->FamilyMAC80211();
      header->nlmsg_pid = 0;
      header->nlmsg_seq = 0;
      header->nlmsg_flags = NLM_F_REQUEST;
      genhdr->cmd = WIFIROUTER_CMD_SEND;
      Cmd c(m);
      nl_->WRCL().Send(&c);
      c.WaitComplete();
    } else {
      shm_wifi_.Send(header, header->nlmsg_len);
    }
  }
}

}  // namespace cvd
