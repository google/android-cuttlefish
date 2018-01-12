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
#include "common/libs/wifi/router.h"

#ifdef CUTTLEFISH_HOST
#include "host/libs/config/host_config.h"
#endif

namespace cvd {

PacketSwitch::~PacketSwitch() { Stop(); }

bool PacketSwitch::Init() {
#ifdef CUTTLEFISH_HOST
  return shm_wifi_.Open(vsoc::GetDomain().c_str());
#else
  return shm_wifi_.Open();
#endif
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
      while (started_) {
        // TODO(ender): how to trigger (periodic?) exit from this call?
        auto len = shm_wifi_.Recv(msg.get(), maxlen);
        std::unique_ptr<nl_msg, void (*)(nl_msg*)> nlm(
            nlmsg_convert(reinterpret_cast<nlmsghdr*>(msg.get())), nlmsg_free);
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
    // This attribute is mandatory: it contains MAC80211_HWSIM frame.
    auto packet =
        nlmsg_find_attr(header, sizeof(*genhdr), WIFIROUTER_ATTR_PACKET);
    if (!packet) return;

    // If origin is not local (= not set from local WIFI), then forward it to
    // local WIFI.
    if (is_incoming) {
      // Need to update MAC80211_HWSIM WIFI family before injecting packet.
      // Different kernels may have different family numbers allocated.
      auto frame = reinterpret_cast<nlmsghdr*>(nla_data(packet));
      frame->nlmsg_type = nl_->FamilyMAC80211();
      frame->nlmsg_pid = 0;
      frame->nlmsg_seq = 0;
      frame->nlmsg_flags = NLM_F_REQUEST;
      Cmd local(frame);

      nl_->GeNL().Send(&local);

      for (auto* r : local.Responses()) {
        auto hdr = nlmsg_hdr(r);
        if (hdr->nlmsg_type == NLMSG_ERROR) {
          nlmsgerr* err = static_cast<nlmsgerr*>(nlmsg_data(hdr));
          if (err->error < 0) {
            LOG(ERROR) << "Could not send WIFI message: "
                       << strerror(-err->error);
          }
        }
      }
    } else {
      shm_wifi_.Send(&header, header->nlmsg_len);
    }
  }
}

}  // namespace cvd
