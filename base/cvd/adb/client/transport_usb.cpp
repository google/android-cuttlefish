/*
 * Copyright (C) 2007 The Android Open Source Project
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

#define TRACE_TAG TRANSPORT

#include "sysdeps.h"

#include "client/usb.h"

#include <memory>

#include "sysdeps.h"
#include "transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adb.h"

#if defined(__APPLE__)
#define CHECK_PACKET_OVERFLOW 0
#else
#define CHECK_PACKET_OVERFLOW 1
#endif

// Call usb_read using a buffer having a multiple of usb_get_max_packet_size() bytes
// to avoid overflow. See http://libusb.sourceforge.net/api-1.0/packetoverflow.html.
static int UsbReadMessage(usb_handle* h, amessage* msg) {
    D("UsbReadMessage");

#if CHECK_PACKET_OVERFLOW
    size_t usb_packet_size = usb_get_max_packet_size(h);
    CHECK_GE(usb_packet_size, sizeof(*msg));
    CHECK_LT(usb_packet_size, 4096ULL);

    char buffer[4096];
    int n = usb_read(h, buffer, usb_packet_size);
    if (n != sizeof(*msg)) {
        D("usb_read returned unexpected length %d (expected %zu)", n, sizeof(*msg));
        return -1;
    }
    memcpy(msg, buffer, sizeof(*msg));
    return n;
#else
    return usb_read(h, msg, sizeof(*msg));
#endif
}

// Call usb_read using a buffer having a multiple of usb_get_max_packet_size() bytes
// to avoid overflow. See http://libusb.sourceforge.net/api-1.0/packetoverflow.html.
static int UsbReadPayload(usb_handle* h, apacket* p) {
    D("UsbReadPayload(%d)", p->msg.data_length);

    if (p->msg.data_length > MAX_PAYLOAD) {
        return -1;
    }

#if CHECK_PACKET_OVERFLOW
    size_t usb_packet_size = usb_get_max_packet_size(h);

    // Round the data length up to the nearest packet size boundary.
    // The device won't send a zero packet for packet size aligned payloads,
    // so don't read any more packets than needed.
    size_t len = p->msg.data_length;
    size_t rem_size = len % usb_packet_size;
    if (rem_size) {
        len += usb_packet_size - rem_size;
    }

    p->payload.resize(len);
    int rc = usb_read(h, &p->payload[0], p->payload.size());
    if (rc != static_cast<int>(p->msg.data_length)) {
        return -1;
    }

    p->payload.resize(rc);
    return rc;
#else
    p->payload.resize(p->msg.data_length);
    return usb_read(h, &p->payload[0], p->payload.size());
#endif
}

static int remote_read(apacket* p, usb_handle* usb) {
    int n = UsbReadMessage(usb, &p->msg);
    if (n < 0) {
        D("remote usb: read terminated (message)");
        return -1;
    }
    if (static_cast<size_t>(n) != sizeof(p->msg)) {
        D("remote usb: read received unexpected header length %d", n);
        return -1;
    }
    if (p->msg.data_length) {
        n = UsbReadPayload(usb, p);
        if (n < 0) {
            D("remote usb: terminated (data)");
            return -1;
        }
        if (static_cast<uint32_t>(n) != p->msg.data_length) {
            D("remote usb: read payload failed (need %u bytes, give %d bytes), skip it",
              p->msg.data_length, n);
            return -1;
        }
    }
    return 0;
}

UsbConnection::~UsbConnection() {
    usb_close(handle_);
}

bool UsbConnection::Read(apacket* packet) {
    int rc = remote_read(packet, handle_);
    return rc == 0;
}

bool UsbConnection::Write(apacket* packet) {
    int size = packet->msg.data_length;

    if (usb_write(handle_, &packet->msg, sizeof(packet->msg)) != sizeof(packet->msg)) {
        PLOG(ERROR) << "remote usb: 1 - write terminated";
        return false;
    }

    if (packet->msg.data_length != 0 && usb_write(handle_, packet->payload.data(), size) != size) {
        PLOG(ERROR) << "remote usb: 2 - write terminated";
        return false;
    }

    return true;
}

bool UsbConnection::DoTlsHandshake(RSA* key, std::string* auth_key) {
    // TODO: support TLS for usb connections
    LOG(FATAL) << "Not supported yet.";
    return false;
}

void UsbConnection::Reset() {
    usb_reset(handle_);
    usb_kick(handle_);
}

void UsbConnection::Close() {
    usb_kick(handle_);
}

void init_usb_transport(atransport* t, usb_handle* h) {
    D("transport: usb");
    auto connection = std::make_unique<UsbConnection>(h);
    t->SetConnection(std::make_unique<BlockingConnectionAdapter>(std::move(connection)));
    t->SetUsbHandle(h);
}

bool is_adb_interface(int usb_class, int usb_subclass, int usb_protocol) {
    // ADB over gadget mode and DbC use the same ADB protocol.
    if (usb_protocol == ADB_PROTOCOL && ((usb_class == ADB_CLASS && usb_subclass == ADB_SUBCLASS) ||
            (usb_class == ADB_DBC_CLASS && usb_subclass == ADB_DBC_SUBCLASS)))
        return true;
    else
        return false;
}

bool is_libusb_enabled() {
    bool enable = true;
#if defined(_WIN32)
    enable = false;
#endif
    char* env = getenv("ADB_LIBUSB");
    if (env) {
        enable = (strcmp(env, "1") == 0);
    }
    return enable;
}
