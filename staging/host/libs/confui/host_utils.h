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

#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include <android-base/logging.h>

#include "common/libs/confui/confui.h"
#include "common/libs/utils/contains.h"
#include "host/commands/kernel_log_monitor/utils.h"
#include "host/libs/config/logging.h"

namespace cuttlefish {
namespace confui {

namespace thread {
/* thread id to name
 * these three functions internally uses the singleton ThreadTracer object.
 *
 * When running a thread, use the global RunThread function
 */
std::string GetName(const std::thread::id tid = std::this_thread::get_id());
std::optional<std::thread::id> GetId(const std::string& name);
void Set(const std::string& name, const std::thread::id tid);

/*
 * This is wrapping std::thread. However, we keep the bidirectional map
 * between the given thread name and the thread id. The main purpose is
 * to help debugging.
 *
 */
template <typename F, typename... Args>
std::thread RunThread(const std::string& name, F&& f, Args&&... args);

class ThreadTracer;
ThreadTracer& GetThreadTracer();

class ThreadTracer {
  friend ThreadTracer& GetThreadTracer();
  friend std::string GetName(const std::thread::id tid);
  friend std::optional<std::thread::id> GetId(const std::string& name);
  friend void Set(const std::string& name, const std::thread::id tid);

  template <typename F, typename... Args>
  friend std::thread RunThread(const std::string& name, F&& f, Args&&... args);

 private:
  template <typename F, typename... Args>
  std::thread RunThread(const std::string& name, F&& f, Args&&... args) {
    auto th = std::thread(std::forward<F>(f), std::forward<Args>(args)...);
    if (Contains(name2id_, name)) {
      ConfUiLog(FATAL) << "Thread name is duplicated";
    }
    name2id_[name] = th.get_id();
    id2name_[th.get_id()] = name;
    ConfUiLog(DEBUG) << name << "thread started.";
    return th;
  }
  std::string Get(const std::thread::id id = std::this_thread::get_id());
  std::optional<std::thread::id> Get(const std::string& name);

  // add later on even though it wasn't started with RunThread
  // if tid is already added, update the name only
  void Set(const std::string& name, const std::thread::id tid);

  ThreadTracer() = default;
  std::map<std::thread::id, std::string> id2name_;
  std::map<std::string, std::thread::id> name2id_;
  std::mutex mtx_;
};

template <typename F, typename... Args>
std::thread RunThread(const std::string& name, F&& f, Args&&... args) {
  auto& tracer = GetThreadTracer();
  return tracer.RunThread(name, std::forward<F>(f),
                          std::forward<Args>(args)...);
}

}  // namespace thread
}  // namespace confui
}  // namespace cuttlefish
