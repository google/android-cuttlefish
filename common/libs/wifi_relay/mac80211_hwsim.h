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

#pragma once

#include "common/vsoc/lib/wifi_exchange_view.h"

#include <errno.h>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <netlink/netlink.h>
#include <vector>

struct Mac80211HwSim {
    using MacAddress = std::vector<uint8_t>;

    static constexpr size_t kMessageSizeMax = 128 * 1024;

    explicit Mac80211HwSim(const MacAddress &mac);
    Mac80211HwSim(const Mac80211HwSim &) = delete;
    Mac80211HwSim &operator=(const Mac80211HwSim &) = delete;

    virtual ~Mac80211HwSim() = default;

    int initCheck() const;

    int socketFd() const;

    void handlePacket();

    int mac80211Family() const { return mMac80211Family; }
    int nl80211Family() const { return mNl80211Family; }

    int addRemote(
            const MacAddress &mac,
            vsoc::wifi::WifiExchangeView *wifiExchange);

    void removeRemote(const MacAddress &mac);

    static bool ParseMACAddress(const std::string &s, MacAddress *mac);

private:
    struct Remote {
        explicit Remote(
            Mac80211HwSim *parent,
            vsoc::wifi::WifiExchangeView *wifiExchange);

        Remote(const Remote &) = delete;
        Remote &operator=(const Remote &) = delete;

        virtual ~Remote();

        intptr_t send(const void *data, size_t size);

    private:
        Mac80211HwSim *mParent;
        vsoc::wifi::WifiExchangeView *mWifiExchange;
        std::unique_ptr<vsoc::RegionWorker> mWifiWorker;

        volatile bool mDone = false;
        std::unique_ptr<std::thread> mThread;
    };

    int mInitCheck = -ENODEV;
    MacAddress mMAC;
    std::unique_ptr<nl_sock, void (*)(nl_sock *)> mSock;
    int mMac80211Family = 0;
    int mNl80211Family = 0;

    std::mutex mRemotesLock;
    std::map<MacAddress, std::unique_ptr<Remote>> mRemotes;

    void injectMessage(nlmsghdr *hdr);
    void injectFrame(const void *data, size_t size);
    void ackFrame(nlmsghdr *msg);
    void dumpMessage(nlmsghdr *msg) const;
    int registerOrSubscribe(const MacAddress &mac);
};
