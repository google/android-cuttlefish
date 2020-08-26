/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <memory>

#include "common/libs/fs/shared_fd.h"
#include "host/frontend/webrtc/display_handler.h"
#include "host/frontend/webrtc/lib/connection_observer.h"

namespace cuttlefish {

class CfConnectionObserverFactory
    : public cuttlefish::webrtc_streaming::ConnectionObserverFactory {
 public:
  CfConnectionObserverFactory(cuttlefish::SharedFD touch_fd,
                              cuttlefish::SharedFD keyboard_fd);
  ~CfConnectionObserverFactory() override = default;

  std::shared_ptr<cuttlefish::webrtc_streaming::ConnectionObserver> CreateObserver()
      override;

  void SetDisplayHandler(std::weak_ptr<DisplayHandler> display_handler);

 private:
  cuttlefish::SharedFD touch_fd_;
  cuttlefish::SharedFD keyboard_fd_;
  std::weak_ptr<DisplayHandler> weak_display_handler_;
};

}  // namespace cuttlefish
