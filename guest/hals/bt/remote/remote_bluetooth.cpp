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

#define LOG_TAG "android.hardware.bluetooth@1.1.remote"

#include "remote_bluetooth.h"

#include <cutils/properties.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/uio.h>
#include <termios.h>
#include <utils/Log.h>
#include "log/log.h"

namespace {
int SetTerminalRaw(int fd) {
  termios terminal_settings;
  int rval = tcgetattr(fd, &terminal_settings);
  if (rval < 0) {
    return rval;
  }
  cfmakeraw(&terminal_settings);
  rval = tcsetattr(fd, TCSANOW, &terminal_settings);
  return rval;
}
}  // namespace

namespace android {
namespace hardware {
namespace bluetooth {
namespace V1_1 {
namespace remote {

using ::android::hardware::hidl_vec;

class BluetoothDeathRecipient : public hidl_death_recipient {
 public:
  BluetoothDeathRecipient(const sp<IBluetoothHci> hci) : mHci(hci) {}

  void serviceDied(
      uint64_t /* cookie */,
      const wp<::android::hidl::base::V1_0::IBase>& /* who */) override {
    LOG(ERROR)
        << "BluetoothDeathRecipient::serviceDied - Bluetooth service died";
    has_died_ = true;
    mHci->close();
  }
  sp<IBluetoothHci> mHci;
  bool getHasDied() const { return has_died_; }
  void setHasDied(bool has_died) { has_died_ = has_died; }

 private:
  bool has_died_;
};

BluetoothHci::BluetoothHci(const std::string& dev_path)
    : death_recipient_(new BluetoothDeathRecipient(this)),
      dev_path_(dev_path) {}

Return<void> BluetoothHci::initialize(
    const sp<V1_0::IBluetoothHciCallbacks>& cb) {
  return initialize_impl(cb, nullptr);
}

Return<void> BluetoothHci::initialize_1_1(
    const sp<V1_1::IBluetoothHciCallbacks>& cb) {
  return initialize_impl(cb, cb);
}

Return<void> BluetoothHci::initialize_impl(
    const sp<V1_0::IBluetoothHciCallbacks>& cb,
    const sp<V1_1::IBluetoothHciCallbacks>& cb_1_1) {
  LOG(INFO) << __func__;

  cb_ = cb;
  cb_1_1_ = cb_1_1;
  fd_ = open(dev_path_.c_str(), O_RDWR);
  if (fd_ < 0) {
    LOG(FATAL) << "Could not connect to bt: " << fd_;
  }
  if (int ret = SetTerminalRaw(fd_) < 0) {
    LOG(FATAL) << "Could not make " << fd_ << " a raw terminal: " << ret;
  }

  if (cb == nullptr) {
    LOG(ERROR)
        << "cb == nullptr! -> Unable to call initializationComplete(ERR)";
    return Void();
  }

  death_recipient_->setHasDied(false);
  auto link_ret = cb->linkToDeath(death_recipient_, 0);

  unlink_cb_ = [this, cb](sp<BluetoothDeathRecipient>& death_recipient) {
    if (death_recipient->getHasDied())
      LOG(INFO) << "Skipping unlink call, service died.";
    else {
      auto ret = cb->unlinkToDeath(death_recipient);
      if (!ret.isOk()) {
        CHECK(death_recipient_->getHasDied())
            << "Error calling unlink, but no death notification.";
      }
    }
  };

  auto init_ret = cb->initializationComplete(V1_0::Status::SUCCESS);
  if (!init_ret.isOk()) {
    CHECK(death_recipient_->getHasDied())
        << "Error sending init callback, but no death notification.";
  }
  h4_ = rootcanal::H4Packetizer(
      fd_,
      [](const std::vector<uint8_t>& /* raw_command */) {
        LOG_ALWAYS_FATAL("Unexpected command!");
      },
      [this](const std::vector<uint8_t>& raw_event) {
        cb_->hciEventReceived(hidl_vec<uint8_t>(raw_event));
      },
      [this](const std::vector<uint8_t>& raw_acl) {
        cb_->hciEventReceived(hidl_vec<uint8_t>(raw_acl));
      },
      [this](const std::vector<uint8_t>& raw_sco) {
        cb_->hciEventReceived(hidl_vec<uint8_t>(raw_sco));
      },
      [this](const std::vector<uint8_t>& raw_iso) {
        if (cb_1_1_) {
          cb_1_1_->hciEventReceived(hidl_vec<uint8_t>(raw_iso));
        }
      },
      []() { LOG(INFO) << "HCI socket device disconnected"; });
  fd_watcher_.WatchFdForNonBlockingReads(
      fd_, [this](int fd) { h4_.OnDataReady(fd); });
  return Void();
}

Return<void> BluetoothHci::close() {
  LOG(INFO) << __func__;
  fd_watcher_.StopWatchingFileDescriptors();
  ::close(fd_);

  return Void();
}

Return<void> BluetoothHci::sendHciCommand(const hidl_vec<uint8_t>& packet) {
  send(rootcanal::PacketType::COMMAND, packet);
  return Void();
}

Return<void> BluetoothHci::sendAclData(const hidl_vec<uint8_t>& packet) {
  send(rootcanal::PacketType::ACL, packet);
  return Void();
}

Return<void> BluetoothHci::sendScoData(const hidl_vec<uint8_t>& packet) {
  send(rootcanal::PacketType::SCO, packet);
  return Void();
}

Return<void> BluetoothHci::sendIsoData(const hidl_vec<uint8_t>& packet) {
  send(rootcanal::PacketType::ISO, packet);
  return Void();
}

void BluetoothHci::send(rootcanal::PacketType type,
                        const ::android::hardware::hidl_vec<uint8_t>& v) {
  h4_.Send(static_cast<uint8_t>(type), v.data(), v.size());
}

/* Fallback to shared library if there is no service. */
IBluetoothHci* HIDL_FETCH_IBluetoothHci(const char* /* name */) {
  return new BluetoothHci();
}

}  // namespace remote
}  // namespace V1_1
}  // namespace bluetooth
}  // namespace hardware
}  // namespace android
