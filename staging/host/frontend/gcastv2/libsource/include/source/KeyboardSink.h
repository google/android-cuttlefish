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

#include <cinttypes>
#include <memory>

#include <https/RunLoop.h>
#include <source/InputSink.h>

namespace android {

struct KeyboardSink {
  explicit KeyboardSink(std::shared_ptr<RunLoop> runLoop, int serverFd,
                        bool write_virtio_input);
  ~KeyboardSink() = default;

  void start() {sink_->start();}

  void injectEvent(bool down, uint16_t code);

 private:
  std::shared_ptr<InputSink> sink_;
};

}  // namespace android

