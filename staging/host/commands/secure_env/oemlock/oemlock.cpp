/*
 * Copyright 2023 The Android Open Source Project
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
 *
 */

#include "host/commands/secure_env/oemlock/oemlock.h"

namespace cuttlefish {
namespace oemlock {
namespace {

constexpr char kStateKey[] = "oemlock_state";
constexpr int kAllowedByCarrierBit = 0;
constexpr int kAllowedByDeviceBit = 1;
constexpr int kOemLockedBit = 2;

// Default state is allowed_by_carrier = true
//                  allowed_by_device = false
//                  locked = true
constexpr uint8_t kDefaultState = 0 | (1 << kAllowedByCarrierBit);

Result<void> InitializeDefaultState(secure_env::Storage& storage) {
  if (storage.Exists()) { return {}; };
  auto data = CF_EXPECT(secure_env::CreateStorageData(&kDefaultState, sizeof(kDefaultState)));
  CF_EXPECT(storage.Write(kStateKey, *data));
  return {};
}

Result<bool> ReadFlag(secure_env::Storage& storage, int bit) {
  auto data = CF_EXPECT(storage.Read(kStateKey));
  auto state = CF_EXPECT(data->asUint8());
  return (state >> bit) & 1;
}

Result<void> WriteFlag(secure_env::Storage& storage, int bit, bool value) {
  auto data = CF_EXPECT(storage.Read(kStateKey));
  auto state = CF_EXPECT(data->asUint8());
  value ? state |= (1 << bit) : state &= ~(1 << bit);
  auto data_to_write = CF_EXPECT(secure_env::CreateStorageData(&state, sizeof(state)));
  CF_EXPECT(storage.Write(kStateKey, *data_to_write));
  return {};
}

} // namespace

OemLock::OemLock(secure_env::Storage& storage) : storage_(storage) {
  auto result = InitializeDefaultState(storage_);
  if (!result.ok()) {
    LOG(FATAL) << "Failed to initialize default state for OemLock TEE storage: "
               << result.error().Message();
  }
}

Result<bool> OemLock::IsOemUnlockAllowedByCarrier() const {
  return CF_EXPECT(ReadFlag(storage_, kAllowedByCarrierBit));
}

Result<bool> OemLock::IsOemUnlockAllowedByDevice() const {
  return CF_EXPECT(ReadFlag(storage_, kAllowedByDeviceBit));
}

Result<bool> OemLock::IsOemUnlockAllowed() const {
  auto data = CF_EXPECT(storage_.Read(kStateKey));
  auto state = CF_EXPECT(data->asUint8());
  const bool allowed_by_device = (state >> kAllowedByDeviceBit) & 1;
  const bool allowed_by_carrier = (state >> kAllowedByCarrierBit) & 1;
  return allowed_by_device && allowed_by_carrier;
}

Result<bool> OemLock::IsOemLocked() const {
  return CF_EXPECT(ReadFlag(storage_, kOemLockedBit));
}

Result<void> OemLock::SetOemUnlockAllowedByCarrier(bool allowed) {
  CF_EXPECT(WriteFlag(storage_, kAllowedByCarrierBit, allowed));
  return {};
}

Result<void> OemLock::SetOemUnlockAllowedByDevice(bool allowed) {
  CF_EXPECT(WriteFlag(storage_, kAllowedByDeviceBit, allowed));
  return {};
}

Result<void> OemLock::SetOemLocked(bool locked) {
  CF_EXPECT(WriteFlag(storage_, kOemLockedBit, locked));
  return {};
}

} // namespace oemlock
} // namespace cuttlefish