//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <android-base/logging.h>
#include <fruit/fruit.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/feature.h"

namespace cuttlefish {

class WebRtcRecorder : public SetupFeature {
 public:
  INJECT(WebRtcRecorder()) {};
  std::string Name() const override { return "WebRtcRecorder"; }
  bool Enabled() const override { return true; }
  Result<void> ResultSetup() override;

  SharedFD GetClientSocket() const;
  Result<void> SendStartRecordingCommand() const;
  Result<void> SendStopRecordingCommand() const;


 protected:
  SharedFD client_socket_;
  SharedFD host_socket_;


 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }

  Result<void> SendCommandAndVerifyResponse(std::string message) const;
};

fruit::Component<WebRtcRecorder> WebRtcRecorderComponent();

}  // namespace cuttlefish
