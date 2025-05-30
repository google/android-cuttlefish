/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <sys/types.h>

#include "adb.h"
#include "transport.h"

// USB host/client interface.

struct usb_handle;

void usb_init();
void usb_cleanup();
int usb_write(usb_handle* h, const void* data, int len);
int usb_read(usb_handle* h, void* data, int len);
int usb_close(usb_handle* h);
void usb_reset(usb_handle* h);
void usb_kick(usb_handle* h);
size_t usb_get_max_packet_size(usb_handle*);

// USB device detection.
bool is_adb_interface(int usb_class, int usb_subclass, int usb_protocol);

bool is_libusb_enabled();

struct UsbConnection : public BlockingConnection {
    explicit UsbConnection(usb_handle* handle) : handle_(handle) {}
    ~UsbConnection();

    bool Read(apacket* packet) override final;
    bool Write(apacket* packet) override final;
    bool DoTlsHandshake(RSA* key, std::string* auth_key) override final;

    void Close() override final;
    virtual void Reset() override final;

    usb_handle* handle_;
};
