/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "host/commands/kernel_log_monitor/kernel_log_server.h"

#include <map>
#include <utility>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <netinet/in.h>
#include "common/libs/fs/shared_select.h"
#include "host/libs/config/cuttlefish_config.h"

using cuttlefish::SharedFD;

namespace {
static const std::map<std::string, std::string> kInformationalPatterns = {
    {"U-Boot ", "GUEST_UBOOT_VERSION: "},
    {"] Linux version ", "GUEST_KERNEL_VERSION: "},
    {"GUEST_BUILD_FINGERPRINT: ", "GUEST_BUILD_FINGERPRINT: "},
};

static const std::map<std::string, monitor::Event> kStageToEventMap = {
    {cuttlefish::kBootStartedMessage, monitor::Event::BootStarted},
    {cuttlefish::kBootCompletedMessage, monitor::Event::BootCompleted},
    {cuttlefish::kBootFailedMessage, monitor::Event::BootFailed},
    {cuttlefish::kMobileNetworkConnectedMessage,
     monitor::Event::MobileNetworkConnected},
    {cuttlefish::kWifiConnectedMessage, monitor::Event::WifiNetworkConnected},
    {cuttlefish::kEthernetConnectedMessage, monitor::Event::EthernetNetworkConnected},
    // TODO(b/131864854): Replace this with a string less likely to change
    {"init: starting service 'adbd'...", monitor::Event::AdbdStarted},
    {cuttlefish::kScreenChangedMessage, monitor::Event::ScreenChanged},
};

void ProcessSubscriptions(
    Json::Value message,
    std::vector<monitor::EventCallback>* subscribers) {
  auto active_subscription_count = subscribers->size();
  std::size_t idx = 0;
  while (idx < active_subscription_count) {
    // Call the callback
    auto action = (*subscribers)[idx](message);
    if (action == monitor::SubscriptionAction::ContinueSubscription) {
      ++idx;
    } else {
      // Cancel the subscription by swaping it with the last active subscription
      // and decreasing the active subscription count
      --active_subscription_count;
      std::swap((*subscribers)[idx], (*subscribers)[active_subscription_count]);
    }
  }
  // Keep only the active subscriptions
  subscribers->resize(active_subscription_count);
}
}  // namespace

namespace monitor {
KernelLogServer::KernelLogServer(cuttlefish::SharedFD pipe_fd,
                                 const std::string& log_name,
                                 bool deprecated_boot_completed)
    : pipe_fd_(pipe_fd),
      log_fd_(cuttlefish::SharedFD::Open(log_name.c_str(), O_CREAT | O_RDWR | O_APPEND, 0666)),
      deprecated_boot_completed_(deprecated_boot_completed) {}

void KernelLogServer::BeforeSelect(cuttlefish::SharedFDSet* fd_read) const {
  fd_read->Set(pipe_fd_);
}

void KernelLogServer::AfterSelect(const cuttlefish::SharedFDSet& fd_read) {
  if (fd_read.IsSet(pipe_fd_)) {
    HandleIncomingMessage();
  }
}

void KernelLogServer::SubscribeToEvents(monitor::EventCallback callback) {
  subscribers_.push_back(callback);
}

bool KernelLogServer::HandleIncomingMessage() {
  const size_t buf_len = 256;
  char buf[buf_len];
  ssize_t ret = pipe_fd_->Read(buf, buf_len);
  if (ret < 0) {
    LOG(ERROR) << "Could not read kernel logs: " << pipe_fd_->StrError();
    return false;
  }
  if (ret == 0) return false;
  // Write the log to a file
  if (log_fd_->Write(buf, ret) < 0) {
    LOG(ERROR) << "Could not write kernel log to file: " << log_fd_->StrError();
    return false;
  }

  // Detect VIRTUAL_DEVICE_BOOT_*
  for (ssize_t i=0; i<ret; i++) {
    if ('\n' == buf[i]) {
      for (auto& info_kv : kInformationalPatterns) {
        auto& match = info_kv.first;
        auto& prefix = info_kv.second;
        auto pos = line_.find(match);
        if (std::string::npos != pos) {
          LOG(INFO) << prefix << line_.substr(pos + match.size());
        }
      }
      for (auto& stage_kv : kStageToEventMap) {
        auto& stage = stage_kv.first;
        auto event = stage_kv.second;
        auto pos = line_.find(stage);
        if (std::string::npos != pos) {
          // Log the stage
          LOG(INFO) << stage;

          Json::Value message;
          message["event"] = event;
          Json::Value metadata;
          // Expect space-separated key=value pairs in the log message.
          const auto& fields = android::base::Split(
              line_.substr(pos + stage.size()), " ");
          for (std::string field : fields) {
            field = android::base::Trim(field);
            if (field.empty()) {
              // Expected; android::base::Split() always returns at least
              // one (possibly empty) string.
              LOG(DEBUG) << "Empty field for line: " << line_;
              continue;
            }
            const auto& keyvalue = android::base::Split(field, "=");
            if (keyvalue.size() != 2) {
              LOG(WARNING) << "Field is not in key=value format: " << field;
              continue;
            }
            metadata[keyvalue[0]] = keyvalue[1];
          }
          message["metadata"] = metadata;
          ProcessSubscriptions(message, &subscribers_);

          //TODO(b/69417553) Remove this when our clients have transitioned to the
          // new boot completed
          if (deprecated_boot_completed_) {
            // Write to host kernel log
            FILE* log = popen("/usr/bin/sudo /usr/bin/tee /dev/kmsg", "w");
            fprintf(log, "%s\n", stage.c_str());
            fclose(log);
          }
        }
      }
      line_.clear();
    }
    line_.append(1, buf[i]);
  }

  return true;
}

}  // namespace monitor
