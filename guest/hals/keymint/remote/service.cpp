/*
 * Copyright 2020, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "android.hardware.security.keymint-service.remote"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <keymaster/android_keymaster_messages.h>
#include <keymaster/km_version.h>
#include <keymaster/soft_keymaster_logger.h>

#include <aidl/android/hardware/security/keymint/SecurityLevel.h>
#include <guest/hals/keymint/remote/remote_keymaster.h>
#include <guest/hals/keymint/remote/remote_keymint_device.h>
#include <guest/hals/keymint/remote/remote_remotely_provisioned_component.h>
#include <guest/hals/keymint/remote/remote_secure_clock.h>
#include <guest/hals/keymint/remote/remote_shared_secret.h>
#include "common/libs/fs/shared_fd.h"
#include "common/libs/security/keymaster_channel.h"

namespace {

const char device[] = "/dev/hvc3";

using aidl::android::hardware::security::keymint::RemoteKeyMintDevice;
using aidl::android::hardware::security::keymint::
    RemoteRemotelyProvisionedComponent;
using aidl::android::hardware::security::keymint::SecurityLevel;
using aidl::android::hardware::security::secureclock::RemoteSecureClock;
using aidl::android::hardware::security::sharedsecret::RemoteSharedSecret;
using keymaster::GenerateTimestampTokenRequest;
using keymaster::GenerateTimestampTokenResponse;

template <typename T, class... Args>
std::shared_ptr<T> addService(Args&&... args) {
  std::shared_ptr<T> ser =
      ndk::SharedRefBase::make<T>(std::forward<Args>(args)...);
  auto instanceName = std::string(T::descriptor) + "/default";
  LOG(INFO) << "adding keymint service instance: " << instanceName;
  binder_status_t status =
      AServiceManager_addService(ser->asBinder().get(), instanceName.c_str());
  CHECK(status == STATUS_OK);
  return ser;
}

SecurityLevel getSecurityLevel(::keymaster::RemoteKeymaster& remote_keymaster) {
  GenerateTimestampTokenRequest request(remote_keymaster.message_version());
  GenerateTimestampTokenResponse response(remote_keymaster.message_version());
  remote_keymaster.GenerateTimestampToken(request, &response);

  if (response.error != STATUS_OK) {
    LOG(FATAL) << "Error getting timestamp token from remote keymaster: "
               << response.error;
  }

  return static_cast<SecurityLevel>(response.token.security_level);
}

}  // namespace

int main(int, char** argv) {
  android::base::InitLogging(argv, android::base::KernelLogger);
  // Zero threads seems like a useless pool, but below we'll join this thread to
  // it, increasing the pool size to 1.
  ABinderProcess_setThreadPoolMaxThreadCount(0);
  // Add Keymint Service
  auto fd = cuttlefish::SharedFD::Open(device, O_RDWR);
  if (!fd->IsOpen()) {
    LOG(FATAL) << "Could not connect to keymaster: " << fd->StrError();
  }

  if (fd->SetTerminalRaw() < 0) {
    LOG(FATAL) << "Could not make " << device
               << " a raw terminal: " << fd->StrError();
  }

  cuttlefish::SharedFdKeymasterChannel keymasterChannel(fd, fd);

  keymaster::RemoteKeymaster remote_keymaster(
      &keymasterChannel, keymaster::MessageVersion(
                             keymaster::KmVersion::KEYMINT_3, 0 /* km_date */));

  if (!remote_keymaster.Initialize()) {
    LOG(FATAL) << "Could not initialize keymaster";
  }

  addService<RemoteKeyMintDevice>(remote_keymaster,
                                  getSecurityLevel(remote_keymaster));
  addService<RemoteSecureClock>(remote_keymaster);
  addService<RemoteSharedSecret>(remote_keymaster);
  addService<RemoteRemotelyProvisionedComponent>(remote_keymaster);

  ABinderProcess_joinThreadPool();
  return EXIT_FAILURE;  // should not reach
}
