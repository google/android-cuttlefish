/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "host/commands/cvd/selector/data_viewer.h"

namespace cuttlefish {
namespace selector {
Result<SharedFD> DataViewer::LockBackingFile(int op) const {
  auto fd = SharedFD::Open(backing_file_, O_CREAT | O_RDWR, 0640);
  CF_EXPECTF(fd->IsOpen(), "Failed to open instance database backing file: {}",
             fd->StrError());
  CF_EXPECTF(fd->Flock(op),
             "Failed to acquire lock for instance database backing file: {}",
             fd->StrError());
  return fd;
}

Result<cvd::PersistentData> DataViewer::LoadData(SharedFD fd) const {
  std::string str;
  auto read_size = ReadAll(fd, &str);
  CF_EXPECTF(read_size >= 0, "Failed to read from backing file: {}",
             fd->StrError());
  cvd::PersistentData data;
  data.ParseFromString(str);
  return std::move(data);
}

Result<void> DataViewer::StoreData(SharedFD fd, cvd::PersistentData data) {
  std::string str;
  CF_EXPECT(data.SerializeToString(&str), "Failed to serialize data");
  auto write_size = WriteAll(fd, str);
  CF_EXPECTF(write_size == (ssize_t)str.size(),
             "Failed to write to backing file: {}", fd->StrError());
  return {};
}

DataViewer::DeadlockProtector::DeadlockProtector(const DataViewer& dv)
    : mtx_(dv.lock_map_mtx_), map_(dv.lock_held_by_) {
  std::lock_guard lock(mtx_);
  CHECK(!map_[std::this_thread::get_id()])
      << "Detected deadlock due to method reentry";
  map_[std::this_thread::get_id()] = true;
}

DataViewer::DeadlockProtector::~DeadlockProtector() {
  std::lock_guard lock(mtx_);
  map_[std::this_thread::get_id()] = false;
}

}  // namespace selector
}  // namespace cuttlefish
