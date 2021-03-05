//
// Copyright 2021 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once

#include <android/hardware/bluetooth/1.1/IBluetoothHci.h>

#include <android-base/logging.h>
#include <hidl/MQDescriptor.h>
#include <string>
#include "async_fd_watcher.h"
#include "model/devices/h4_packetizer.h"

namespace android {
namespace hardware {
namespace bluetooth {
namespace V1_1 {
namespace remote {

class BluetoothDeathRecipient;

// This Bluetooth HAL implementation is connected with the root-canal process in
// the host side via virtio-console device(refer to dev_path_). It receives and
// deliver responses and requests from/to Bluetooth HAL.
class BluetoothHci : public IBluetoothHci {
 public:
  // virtio-console device connected with root-canal in the host side.
  BluetoothHci(const std::string& dev_path = "/dev/hvc5");

  ::android::hardware::Return<void> initialize(
      const sp<V1_0::IBluetoothHciCallbacks>& cb) override;
  ::android::hardware::Return<void> initialize_1_1(
      const sp<V1_1::IBluetoothHciCallbacks>& cb) override;

  ::android::hardware::Return<void> sendHciCommand(
      const ::android::hardware::hidl_vec<uint8_t>& packet) override;

  ::android::hardware::Return<void> sendAclData(
      const ::android::hardware::hidl_vec<uint8_t>& packet) override;

  ::android::hardware::Return<void> sendScoData(
      const ::android::hardware::hidl_vec<uint8_t>& packet) override;

  ::android::hardware::Return<void> sendIsoData(
      const ::android::hardware::hidl_vec<uint8_t>& packet) override;

  ::android::hardware::Return<void> close() override;

  static void OnPacketReady();

  static BluetoothHci* get();

 private:
  int fd_{-1};
  ::android::sp<V1_0::IBluetoothHciCallbacks> cb_ = nullptr;
  ::android::sp<V1_1::IBluetoothHciCallbacks> cb_1_1_ = nullptr;

  test_vendor_lib::H4Packetizer h4_{fd_,
                                    [](const std::vector<uint8_t>&) {},
                                    [](const std::vector<uint8_t>&) {},
                                    [](const std::vector<uint8_t>&) {},
                                    [](const std::vector<uint8_t>&) {},
                                    [](const std::vector<uint8_t>&) {},
                                    [] {}};

  ::android::hardware::Return<void> initialize_impl(
      const sp<V1_0::IBluetoothHciCallbacks>& cb,
      const sp<V1_1::IBluetoothHciCallbacks>& cb_1_1);

  sp<BluetoothDeathRecipient> death_recipient_;

  const std::string dev_path_;

  std::function<void(sp<BluetoothDeathRecipient>&)> unlink_cb_;

  ::android::hardware::bluetooth::async::AsyncFdWatcher fd_watcher_;

  void send(test_vendor_lib::PacketType type,
            const ::android::hardware::hidl_vec<uint8_t>& packet);
};

extern "C" IBluetoothHci* HIDL_FETCH_IBluetoothHci(const char* name);

}  // namespace remote
}  // namespace V1_1
}  // namespace bluetooth
}  // namespace hardware
}  // namespace android
