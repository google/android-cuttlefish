/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "Nfc.h"

#include <android-base/logging.h>

#include "Cf_hal_api.h"

namespace aidl {
namespace android {
namespace hardware {
namespace nfc {

std::shared_ptr<INfcClientCallback> Nfc::mCallback = nullptr;
AIBinder_DeathRecipient* clientDeathRecipient = nullptr;

void OnDeath(void* cookie) {
  if (Nfc::mCallback != nullptr &&
      !AIBinder_isAlive(Nfc::mCallback->asBinder().get())) {
    LOG(INFO) << __func__ << " Nfc service has died";
    Nfc* nfc = static_cast<Nfc*>(cookie);
    nfc->close(NfcCloseType::DISABLE);
  }
}

::ndk::ScopedAStatus Nfc::open(
    const std::shared_ptr<INfcClientCallback>& clientCallback) {
  LOG(INFO) << "open";
  if (clientCallback == nullptr) {
    LOG(INFO) << "Nfc::open null callback";
    return ndk::ScopedAStatus::fromServiceSpecificError(
        static_cast<int32_t>(NfcStatus::FAILED));
  } else {
    Nfc::mCallback = clientCallback;

    if (clientDeathRecipient != nullptr) {
      AIBinder_DeathRecipient_delete(clientDeathRecipient);
      clientDeathRecipient = nullptr;
    }
    clientDeathRecipient = AIBinder_DeathRecipient_new(OnDeath);
    auto linkRet =
        AIBinder_linkToDeath(clientCallback->asBinder().get(),
                             clientDeathRecipient, this /* cookie */);
    if (linkRet != STATUS_OK) {
      LOG(ERROR) << __func__ << ": linkToDeath failed: " << linkRet;
      // Just ignore the error.
    }

    int ret = Cf_hal_open(eventCallback, dataCallback);
    return ret == 0 ? ndk::ScopedAStatus::ok()
                    : ndk::ScopedAStatus::fromServiceSpecificError(
                          static_cast<int32_t>(NfcStatus::FAILED));
    return ndk::ScopedAStatus::ok();
  }
  return ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Nfc::close(NfcCloseType type) {
  LOG(INFO) << "close";
  if (Nfc::mCallback == nullptr) {
    LOG(ERROR) << __func__ << " mCallback null";
    return ndk::ScopedAStatus::fromServiceSpecificError(
        static_cast<int32_t>(NfcStatus::FAILED));
  }
  int ret = 0;
  if (type == NfcCloseType::HOST_SWITCHED_OFF) {
    ret = Cf_hal_close_off();
  } else {
    ret = Cf_hal_close();
  }
  AIBinder_DeathRecipient_delete(clientDeathRecipient);
  clientDeathRecipient = nullptr;
  return ret == 0 ? ndk::ScopedAStatus::ok()
                  : ndk::ScopedAStatus::fromServiceSpecificError(
                        static_cast<int32_t>(NfcStatus::FAILED));
}

::ndk::ScopedAStatus Nfc::coreInitialized() {
  LOG(INFO) << "coreInitialized";
  if (Nfc::mCallback == nullptr) {
    LOG(ERROR) << __func__ << "mCallback null";
    return ndk::ScopedAStatus::fromServiceSpecificError(
        static_cast<int32_t>(NfcStatus::FAILED));
  }
  int ret = Cf_hal_core_initialized();

  return ret == 0 ? ndk::ScopedAStatus::ok()
                  : ndk::ScopedAStatus::fromServiceSpecificError(
                        static_cast<int32_t>(NfcStatus::FAILED));
}

::ndk::ScopedAStatus Nfc::factoryReset() {
  LOG(INFO) << "factoryReset";
  Cf_hal_factoryReset();
  return ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Nfc::getConfig(NfcConfig* _aidl_return) {
  LOG(INFO) << "getConfig";
  NfcConfig nfcVendorConfig;
  Cf_hal_getConfig(nfcVendorConfig);

  *_aidl_return = nfcVendorConfig;
  return ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Nfc::powerCycle() {
  LOG(INFO) << "powerCycle";
  if (Nfc::mCallback == nullptr) {
    LOG(ERROR) << __func__ << "mCallback null";
    return ndk::ScopedAStatus::fromServiceSpecificError(
        static_cast<int32_t>(NfcStatus::FAILED));
  }
  return Cf_hal_power_cycle() ? ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(NfcStatus::FAILED))
                              : ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Nfc::preDiscover() {
  LOG(INFO) << "preDiscover";
  if (Nfc::mCallback == nullptr) {
    LOG(ERROR) << __func__ << "mCallback null";
    return ndk::ScopedAStatus::fromServiceSpecificError(
        static_cast<int32_t>(NfcStatus::FAILED));
  }
  return Cf_hal_pre_discover() ? ndk::ScopedAStatus::fromServiceSpecificError(
                                     static_cast<int32_t>(NfcStatus::FAILED))
                               : ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Nfc::write(const std::vector<uint8_t>& data,
                                int32_t* _aidl_return) {
  LOG(INFO) << "write";
  if (Nfc::mCallback == nullptr) {
    LOG(ERROR) << __func__ << "mCallback null";
    return ndk::ScopedAStatus::fromServiceSpecificError(
        static_cast<int32_t>(NfcStatus::FAILED));
  }
  *_aidl_return = Cf_hal_write(data.size(), &data[0]);
  return ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Nfc::setEnableVerboseLogging(bool enable) {
  LOG(INFO) << "setVerboseLogging";
  Cf_hal_setVerboseLogging(enable);
  return ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Nfc::isVerboseLoggingEnabled(bool* _aidl_return) {
  *_aidl_return = Cf_hal_getVerboseLogging();
  return ndk::ScopedAStatus::ok();
}

}  // namespace nfc
}  // namespace hardware
}  // namespace android
}  // namespace aidl
