/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include <hardware/hardware.h>
#include <hardware/gatekeeper.h>
#define LOG_TAG "CuttlefishGatekeeper"
#include <log/log.h>

#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "SoftGateKeeper.h"
#include "SoftGateKeeperDevice.h"

using cuttlefish::SoftGateKeeperDevice;

struct cuttlefish_gatekeeper_device {
    gatekeeper_device device;
    SoftGateKeeperDevice *s_gatekeeper;
};

static cuttlefish_gatekeeper_device s_device;

static int enroll(const struct gatekeeper_device *dev __unused, uint32_t uid,
            const uint8_t *current_password_handle, uint32_t current_password_handle_length,
            const uint8_t *current_password, uint32_t current_password_length,
            const uint8_t *desired_password, uint32_t desired_password_length,
            uint8_t **enrolled_password_handle, uint32_t *enrolled_password_handle_length) {

    SoftGateKeeperDevice *s_gatekeeper = ((cuttlefish_gatekeeper_device*)(dev))->s_gatekeeper;
    ALOGE("called %s with gate keeper %p device %p\n", __func__, s_gatekeeper, dev);
    if (s_gatekeeper == nullptr)  {
        abort();
        return -EINVAL;
    }

    return s_gatekeeper->enroll(uid,
            current_password_handle, current_password_handle_length,
            current_password, current_password_length,
            desired_password, desired_password_length,
            enrolled_password_handle, enrolled_password_handle_length);
}

static int verify(const struct gatekeeper_device *dev __unused, uint32_t uid, uint64_t challenge,
            const uint8_t *enrolled_password_handle, uint32_t enrolled_password_handle_length,
            const uint8_t *provided_password, uint32_t provided_password_length,
            uint8_t **auth_token, uint32_t *auth_token_length, bool *request_reenroll) {
    SoftGateKeeperDevice *s_gatekeeper = ((cuttlefish_gatekeeper_device*)(dev))->s_gatekeeper;
    ALOGE("called %s with gate keeper %p device %p\n", __func__, s_gatekeeper, dev);
    if (s_gatekeeper == nullptr) return -EINVAL;
    return s_gatekeeper->verify(uid, challenge,
            enrolled_password_handle, enrolled_password_handle_length,
            provided_password, provided_password_length,
            auth_token, auth_token_length, request_reenroll);
}

static int close_device(hw_device_t* dev __unused) {
    SoftGateKeeperDevice *s_gatekeeper = ((cuttlefish_gatekeeper_device*)(dev))->s_gatekeeper;
    if (s_gatekeeper == nullptr) return 0;
    delete s_gatekeeper;
    s_gatekeeper = nullptr;
    ALOGE("called %s with gate keeper %p device %p\n", __func__, s_gatekeeper, dev);
    return 0;
}

static int cuttlefish_gatekeeper_open(const hw_module_t *module, const char *name,
        hw_device_t **device) {

    if (strcmp(name, HARDWARE_GATEKEEPER) != 0) {
        abort();
        return -EINVAL;
    }

    memset(&s_device, 0, sizeof(s_device));

    SoftGateKeeperDevice *s_gatekeeper = new SoftGateKeeperDevice();
    if (s_gatekeeper == nullptr) return -ENOMEM;

    s_device.s_gatekeeper = s_gatekeeper;

    s_device.device.common.tag = HARDWARE_DEVICE_TAG;
    s_device.device.common.version = 1;
    s_device.device.common.module = const_cast<hw_module_t *>(module);
    s_device.device.common.close = close_device;

    s_device.device.enroll = enroll;
    s_device.device.verify = verify;
    s_device.device.delete_user = nullptr;
    s_device.device.delete_all_users = nullptr;

    *device = &s_device.device.common;
    ALOGE("called %s with gate keeper %p device %p\n", __func__, s_gatekeeper, *device);

    return 0;
}

static struct hw_module_methods_t gatekeeper_module_methods = {
    .open = cuttlefish_gatekeeper_open,
};

struct gatekeeper_module HAL_MODULE_INFO_SYM __attribute__((visibility("default"))) = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = GATEKEEPER_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = GATEKEEPER_HARDWARE_MODULE_ID,
        .name = "Cuttlefish GateKeeper HAL",
        .author = "The Android Open Source Project",
        .methods = &gatekeeper_module_methods,
        .dso = 0,
        .reserved = {}
    },
};
