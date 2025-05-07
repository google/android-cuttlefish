/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "mdns_tracker.h"

#include <list>
#include <string>

#include <google/protobuf/text_format.h>
#include "adb_host.pb.h"
#include "adb_mdns.h"
#include "adb_trace.h"
#include "adb_wifi.h"
#include "client/discovered_services.h"

struct MdnsTracker {
    explicit MdnsTracker() {}
    asocket socket_;
    bool update_needed_ = true;
};

// Not synchronized because all calls happen on fdevent thread
static std::list<MdnsTracker*> mdns_trackers [[clang::no_destroy]];

static std::string list_mdns_services() {
    adb::proto::MdnsServices services;

    mdns::discovered_services.ForAllServices([&](const mdns::ServiceInfo& service) {
        adb::proto::MdnsService* s = nullptr;

        if (service.service == ADB_FULL_MDNS_SERVICE_TYPE(ADB_MDNS_SERVICE_TYPE)) {
            auto* tcp = services.add_tcp();
            s = tcp->mutable_service();
        } else if (service.service == ADB_FULL_MDNS_SERVICE_TYPE(ADB_MDNS_TLS_PAIRING_TYPE)) {
            auto* pair = services.add_pair();
            s = pair->mutable_service();
        } else if (service.service == ADB_FULL_MDNS_SERVICE_TYPE(ADB_MDNS_TLS_CONNECT_TYPE)) {
            auto* tls = services.add_tls();
            tls->set_known_device(adb_wifi_is_known_host(service.instance));
            s = tls->mutable_service();
        } else {
            LOG(WARNING) << "Unknown service type: " << service;
            return;
        }

        s->set_instance(service.instance);
        s->set_service(service.service);
        s->set_port(service.port);

        s->set_ipv4(service.v4_address_string());
        auto ipv6 = s->add_ipv6();
        ipv6->append(service.v6_address_string());

        if (service.attributes.contains("name")) {
            s->set_product_model(service.attributes.at("name"));
        }
        if (service.attributes.contains("api")) {
            s->set_build_version_sdk_full(service.attributes.at("api"));
        }
    });

    std::string proto;
    services.SerializeToString(&proto);
    return proto;
}

static void mdns_tracker_close(asocket* socket) {
    fdevent_check_looper();
    auto* tracker = reinterpret_cast<MdnsTracker*>(socket);
    asocket* peer = socket->peer;

    VLOG(MDNS) << "mdns tracker removed";
    if (peer) {
        peer->peer = nullptr;
        peer->close(peer);
    }
    mdns_trackers.remove(tracker);
    delete tracker;
}

static int device_tracker_enqueue(asocket* socket, apacket::payload_type) {
    fdevent_check_looper();
    /* you can't read from a device tracker, close immediately */
    mdns_tracker_close(socket);
    return -1;
}

static int mdns_tracker_send(const MdnsTracker* tracker, const std::string& string) {
    fdevent_check_looper();
    asocket* peer = tracker->socket_.peer;

    apacket::payload_type data;
    data.resize(4 + string.size());
    char buf[5];
    snprintf(buf, sizeof(buf), "%04x", static_cast<int>(string.size()));
    memcpy(&data[0], buf, 4);
    memcpy(&data[4], string.data(), string.size());
    return peer->enqueue(peer, std::move(data));
}

static void mdns_tracker_ready(asocket* socket) {
    fdevent_check_looper();
    auto* tracker = reinterpret_cast<MdnsTracker*>(socket);

    // We want to send the service list when the tracker connects
    // for the first time, even if no update occurred.
    if (tracker->update_needed_) {
        tracker->update_needed_ = false;
        mdns_tracker_send(tracker, list_mdns_services());
    }
}

asocket* create_mdns_tracker() {
    fdevent_check_looper();
    auto* tracker = new MdnsTracker();
    VLOG(MDNS) << "mdns tracker created";

    tracker->socket_.enqueue = device_tracker_enqueue;
    tracker->socket_.ready = mdns_tracker_ready;
    tracker->socket_.close = mdns_tracker_close;
    tracker->update_needed_ = true;

    mdns_trackers.emplace_back(tracker);
    return &tracker->socket_;
}

void update_mdns_trackers() {
    fdevent_run_on_looper([=]() {
        for (MdnsTracker* tracker : mdns_trackers) {
            mdns_tracker_send(tracker, list_mdns_services());
        }
    });
}