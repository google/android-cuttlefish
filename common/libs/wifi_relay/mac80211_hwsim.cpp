/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "common/libs/wifi_relay/mac80211_hwsim.h"

#include "common/libs/wifi_relay/mac80211_hwsim_driver.h"

#include <glog/logging.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <signal.h>

static constexpr char kWifiSimFamilyName[] = "MAC80211_HWSIM";
static constexpr char kNl80211FamilyName[] = "nl80211";

static constexpr uint32_t kSignalLevelDefault = -24;

#if !defined(ETH_ALEN)
static constexpr size_t ETH_ALEN = 6;
#endif

Mac80211HwSim::Remote::Remote(
        Mac80211HwSim *parent,
        vsoc::wifi::WifiExchangeView *wifiExchange)
    : mParent(parent),
      mWifiExchange(wifiExchange) {
    mWifiWorker = mWifiExchange->StartWorker();

    mThread.reset(new std::thread([this]{
        std::unique_ptr<uint8_t[]> buf(
            new uint8_t[Mac80211HwSim::kMessageSizeMax]);

        for (;;) {
          intptr_t res =
              mWifiExchange->Recv(buf.get(), Mac80211HwSim::kMessageSizeMax);

          if (res < 0) {
            LOG(ERROR) << "WifiExchangeView::Recv failed w/ res " << res;
            continue;
          }

          // LOG(INFO) << "GUEST->HOST packet of size " << res;

          struct nlmsghdr *hdr = reinterpret_cast<struct nlmsghdr *>(buf.get());

          int len = res;
          while (nlmsg_ok(hdr, len)) {
            mParent->injectMessage(hdr);

            hdr = nlmsg_next(hdr, &len);
          }
        }}));
}

Mac80211HwSim::Remote::~Remote() {
    mDone = true;
    mWifiExchange->InterruptSelf();

    mThread->join();
    mThread.reset();
}

intptr_t Mac80211HwSim::Remote::send(const void *data, size_t size) {
    return mWifiExchange->Send(data, size);
}

Mac80211HwSim::Mac80211HwSim(const MacAddress &mac)
    : mMAC(mac),
      mSock(nullptr, nl_socket_free) {
    int res;

    mSock.reset(nl_socket_alloc());

    if (mSock == nullptr) {
        goto bail;
    }

    res = nl_connect(mSock.get(), NETLINK_GENERIC);
    if (res < 0) {
        LOG(ERROR) << "nl_connect failed (" << nl_geterror(res) << ")";
        mInitCheck = res;
        goto bail;
    }

    nl_socket_disable_seq_check(mSock.get());

    res = nl_socket_set_buffer_size(
            mSock.get(), kMessageSizeMax, kMessageSizeMax);

    if (res < 0) {
        LOG(ERROR)
            << "nl_socket_set_buffer_size failed ("
            << nl_geterror(res)
            << ")";

        mInitCheck = res;
        goto bail;
    }

    mMac80211Family = genl_ctrl_resolve(mSock.get(), kWifiSimFamilyName);
    if (mMac80211Family <= 0) {
        LOG(ERROR) << "genl_ctrl_resolve failed.";
        mInitCheck = -ENODEV;
        goto bail;
    }

    mNl80211Family = genl_ctrl_resolve(mSock.get(), kNl80211FamilyName);
    if (mNl80211Family <= 0) {
        LOG(ERROR) << "genl_ctrl_resolve failed.";
        mInitCheck = -ENODEV;
        goto bail;
    }

#if !defined(CUTTLEFISH_HOST)
    res = registerOrSubscribe(mMAC);

    if (res < 0) {
        mInitCheck = res;
        goto bail;
    }
#endif

    mInitCheck = 0;
    return;

bail:
    ;
}

int Mac80211HwSim::initCheck() const {
    return mInitCheck;
}

int Mac80211HwSim::socketFd() const {
    return nl_socket_get_fd(mSock.get());
}

void Mac80211HwSim::dumpMessage(nlmsghdr *msg) const {
    genlmsghdr *hdr = genlmsg_hdr(msg);

    LOG(VERBOSE) << "message cmd = " << (int)hdr->cmd;

    nlattr *attrs[__HWSIM_ATTR_MAX + 1];
    int res = genlmsg_parse(
            msg,
            0 /* hdrlen */,
            attrs,
            __HWSIM_ATTR_MAX,
            nullptr /* policy */);

    if (res < 0) {
        LOG(ERROR) << "genlmsg_parse failed.";
        return;
    }

    // for HWSIM_CMD_FRAME, the following attributes are present:
    // HWSIM_ATTR_ADDR_TRANSMITTER, HWSIM_ATTR_FRAME, HWSIM_ATTR_FLAGS,
    // HWSIM_ATTR_TX_INFO, HWSIM_ATTR_COOKIE, HWSIM_ATTR_FREQ

    for (size_t i = 0; i < __HWSIM_ATTR_MAX; ++i) {
        if (attrs[i]) {
            LOG(VERBOSE) << "Got attribute " << i;
        }
    }
}

void Mac80211HwSim::injectMessage(nlmsghdr *msg) {
#ifdef CUTTLEFISH_HOST
    LOG(VERBOSE) << "------------------- Guest -> Host -----------------------";
#else
    LOG(VERBOSE) << "------------------- Host -> Guest -----------------------";
#endif
    dumpMessage(msg);

    // Do NOT check nlmsg_type against mMac80211Family, these are dynamically
    // assigned and may not necessarily match across machines!

    genlmsghdr *hdr = genlmsg_hdr(msg);
    if (hdr->cmd != HWSIM_CMD_FRAME) {
        LOG(VERBOSE) << "injectMessage: not cmd HWSIM_CMD_FRAME.";
        return;
    }

    nlattr *attrs[__HWSIM_ATTR_MAX + 1];
    int res = genlmsg_parse(
            msg,
            0 /* hdrlen */,
            attrs,
            __HWSIM_ATTR_MAX,
            nullptr /* policy */);

    if (res < 0) {
        LOG(ERROR) << "genlmsg_parse failed.";
        return;
    }

    nlattr *attr = attrs[HWSIM_ATTR_FRAME];
    if (attr) {
        injectFrame(nla_data(attr), nla_len(attr));
    } else {
        LOG(ERROR) << "injectMessage: no HWSIM_ATTR_FRAME.";
    }
}

void Mac80211HwSim::ackFrame(nlmsghdr *inMsg) {
    nlattr *attrs[__HWSIM_ATTR_MAX + 1];
    int res = genlmsg_parse(
            inMsg,
            0 /* hdrlen */,
            attrs,
            __HWSIM_ATTR_MAX,
            nullptr /* policy */);

    if (res < 0) {
        LOG(ERROR) << "genlmsg_parse failed.";
        return;
    }

    uint32_t flags = nla_get_u32(attrs[HWSIM_ATTR_FLAGS]);

    if (!(flags & HWSIM_TX_CTL_REQ_TX_STATUS)) {
        LOG(VERBOSE) << "Frame doesn't require TX_STATUS.";
        return;
    }

    flags |= HWSIM_TX_STAT_ACK;

    const uint8_t *xmitterAddr =
        static_cast<const uint8_t *>(
                nla_data(attrs[HWSIM_ATTR_ADDR_TRANSMITTER]));

    size_t txRatesLen = nla_len(attrs[HWSIM_ATTR_TX_INFO]);

    const struct hwsim_tx_rate *txRates =
        static_cast<const struct hwsim_tx_rate *>(
                nla_data(attrs[HWSIM_ATTR_TX_INFO]));

    uint64_t cookie = nla_get_u64(attrs[HWSIM_ATTR_COOKIE]);

    std::unique_ptr<nl_msg, void (*)(nl_msg *)> outMsg(
            nlmsg_alloc(), nlmsg_free);

    genlmsg_put(
            outMsg.get(),
            NL_AUTO_PID,
            NL_AUTO_SEQ,
            mMac80211Family,
            0 /* hdrlen */,
            NLM_F_REQUEST,
            HWSIM_CMD_TX_INFO_FRAME,
            0 /* version */);

    nla_put(outMsg.get(), HWSIM_ATTR_ADDR_TRANSMITTER, ETH_ALEN, xmitterAddr);
    nla_put_u32(outMsg.get(), HWSIM_ATTR_FLAGS, flags);
    nla_put_u32(outMsg.get(), HWSIM_ATTR_SIGNAL, kSignalLevelDefault);
    nla_put(outMsg.get(), HWSIM_ATTR_TX_INFO, txRatesLen, txRates);
    nla_put_u64(outMsg.get(), HWSIM_ATTR_COOKIE, cookie);

    res = nl_send_auto_complete(mSock.get(), outMsg.get());
    if (res < 0) {
        LOG(ERROR) << "Sending TX Info failed. (" << nl_geterror(res) << ")";
    } else {
        LOG(VERBOSE) << "Sending TX Info SUCCEEDED.";
    }
}

void Mac80211HwSim::injectFrame(const void *data, size_t size) {
    std::unique_ptr<nl_msg, void (*)(nl_msg *)> msg(nlmsg_alloc(), nlmsg_free);

    genlmsg_put(
            msg.get(),
            NL_AUTO_PID,
            NL_AUTO_SEQ,
            mMac80211Family,
            0 /* hdrlen */,
            NLM_F_REQUEST,
            HWSIM_CMD_FRAME,
            0 /* version */);

    CHECK_EQ(mMAC.size(), static_cast<size_t>(ETH_ALEN));
    nla_put(msg.get(), HWSIM_ATTR_ADDR_RECEIVER, ETH_ALEN, &mMAC[0]);

    nla_put(msg.get(), HWSIM_ATTR_FRAME, size, data);
    nla_put_u32(msg.get(), HWSIM_ATTR_RX_RATE, 1);
    nla_put_u32(msg.get(), HWSIM_ATTR_SIGNAL, kSignalLevelDefault);

    LOG(VERBOSE) << "INJECTING!";
    dumpMessage(nlmsg_hdr(msg.get()));

    int res = nl_send_auto_complete(mSock.get(), msg.get());

    if (res < 0) {
        LOG(ERROR) << "Injection failed. (" << nl_geterror(res) << ")";
    } else {
        LOG(VERBOSE) << "Injection SUCCEEDED.";
    }
}

void Mac80211HwSim::handlePacket() {
    sockaddr_nl from;
    uint8_t *data;

    int len = nl_recv(mSock.get(), &from, &data, nullptr /* creds */);
    if (len == 0) {
        LOG(ERROR) << "nl_recv received EOF.";
        return;
    } else if (len < 0) {
        LOG(ERROR) << "nl_recv failed (" << nl_geterror(len) << ")";
        return;
    }

    std::unique_ptr<nlmsghdr, void (*)(nlmsghdr *)> msg(
            reinterpret_cast<nlmsghdr *>(data),
            [](nlmsghdr *hdr) { free(hdr); });

    if (msg->nlmsg_type != mMac80211Family) {
        LOG(VERBOSE)
            << "Received msg of type other than MAC80211: "
            << msg->nlmsg_type;

        return;
    }

#ifdef CUTTLEFISH_HOST
    LOG(VERBOSE) << "------------------- Host -> Guest -----------------------";
#else
    LOG(VERBOSE) << "------------------- Guest -> Host -----------------------";
#endif

    dumpMessage(msg.get());

#if !defined(CUTTLEFISH_HOST)
    ackFrame(msg.get());
#endif

    std::lock_guard<std::mutex> autoLock(mRemotesLock);
    for (auto &remoteEntry : mRemotes) {
        // TODO(andih): Check which remotes to forward this packet to based
        // on the destination address.
        remoteEntry.second->send(msg.get(), msg->nlmsg_len);
    }
}

int Mac80211HwSim::registerOrSubscribe(const MacAddress &mac) {
    std::unique_ptr<nl_msg, void (*)(nl_msg *)> msg(nullptr, nlmsg_free);

    msg.reset(nlmsg_alloc());

    genlmsg_put(
            msg.get(),
            NL_AUTO_PID,
            NL_AUTO_SEQ,
            mMac80211Family,
            0,
            NLM_F_REQUEST,
#ifdef CUTTLEFISH_HOST
            HWSIM_CMD_SUBSCRIBE,
#else
            HWSIM_CMD_REGISTER,
#endif
            0);

#ifdef CUTTLEFISH_HOST
    nla_put(msg.get(), HWSIM_ATTR_ADDR_RECEIVER, ETH_ALEN, &mac[0]);
#else
    // HWSIM_CMD_REGISTER is a global command not specific to a MAC.
    (void)mac;
#endif

    int res = nl_send_auto_complete(mSock.get(), msg.get());

    if (res < 0) {
        LOG(ERROR)
            << "Registration/subscription failed. (" << nl_geterror(res) << ")";

        return res;
    }

    return 0;
}

int Mac80211HwSim::addRemote(
        const MacAddress &mac,
        vsoc::wifi::WifiExchangeView *wifiExchange) {
#ifdef CUTTLEFISH_HOST
    int res = registerOrSubscribe(mac);

    if (res < 0) {
        return res;
    }
#endif

    std::lock_guard<std::mutex> autoLock(mRemotesLock);

    std::unique_ptr<Remote> remote(new Remote(this, wifiExchange));
    mRemotes.insert(std::make_pair(mac, std::move(remote)));

    return 0;
}

void Mac80211HwSim::removeRemote(const MacAddress &mac) {
    std::lock_guard<std::mutex> autoLock(mRemotesLock);
    auto it = mRemotes.find(mac);
    if (it != mRemotes.end()) {
        mRemotes.erase(it);
    }
}
