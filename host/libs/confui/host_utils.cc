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

#include "host/libs/confui/host_utils.h"

namespace cuttlefish {
namespace confui {
namespace thread {
std::string ThreadTracer::Get(const std::thread::id tid) {
  std::lock_guard<std::mutex> lock(mtx_);
  if (Contains(id2name_, tid)) {
    return id2name_[tid];
  }
  std::stringstream ss;
  ss << "Thread@" << tid;
  return ss.str();
}

void ThreadTracer::Set(const std::string& name, const std::thread::id tid) {
  std::lock_guard<std::mutex> lock(mtx_);
  if (Contains(name2id_, name)) {
    // has the name already
    if (name2id_[name] != tid) {  // used for another thread
      ConfUiLog(FATAL) << "Thread name is duplicated.";
    }
    // name and id are already set correctly
    return;
  }
  if (Contains(id2name_, tid)) {
    // tid exists but has a different name
    name2id_.erase(id2name_[tid]);  // delete old_name -> tid map
  }
  id2name_[tid] = name;
  name2id_[name] = tid;
  return;
}

std::optional<std::thread::id> ThreadTracer::Get(const std::string& name) {
  std::lock_guard<std::mutex> lock(mtx_);
  if (Contains(name2id_, name)) {
    return {name2id_[name]};
  }
  return std::nullopt;
}

ThreadTracer& GetThreadTracer() {
  static ThreadTracer thread_tracer;
  return thread_tracer;
}

std::string GetName(const std::thread::id tid) {
  return GetThreadTracer().Get(tid);
}

std::optional<std::thread::id> GetId(const std::string& name) {
  return GetThreadTracer().Get(name);
}

void Set(const std::string& name, const std::thread::id tid) {
  GetThreadTracer().Set(name, tid);
}
}  // namespace thread
}  // namespace confui
}  // namespace cuttlefish
