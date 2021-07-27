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
#include "stream_buffer_cache.h"
#include <algorithm>

namespace android::hardware::camera::device::V3_4::implementation {

std::shared_ptr<CachedStreamBuffer> StreamBufferCache::get(uint64_t buffer_id) {
  auto id_match =
      [buffer_id](const std::shared_ptr<CachedStreamBuffer>& buffer) {
        return buffer->bufferId() == buffer_id;
      };
  std::lock_guard<std::mutex> lock(mutex_);
  auto found = std::find_if(cache_.begin(), cache_.end(), id_match);
  return (found != cache_.end()) ? *found : nullptr;
}

void StreamBufferCache::remove(uint64_t buffer_id) {
  auto id_match =
      [&buffer_id](const std::shared_ptr<CachedStreamBuffer>& buffer) {
        return buffer->bufferId() == buffer_id;
      };
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.erase(std::remove_if(cache_.begin(), cache_.end(), id_match));
}

void StreamBufferCache::update(const StreamBuffer& buffer) {
  auto id = buffer.bufferId;
  auto id_match = [id](const std::shared_ptr<CachedStreamBuffer>& buffer) {
    return buffer->bufferId() == id;
  };
  std::lock_guard<std::mutex> lock(mutex_);
  auto found = std::find_if(cache_.begin(), cache_.end(), id_match);
  if (found == cache_.end()) {
    cache_.emplace_back(std::make_shared<CachedStreamBuffer>(buffer));
  } else {
    (*found)->importFence(buffer.acquireFence);
  }
}

void StreamBufferCache::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.clear();
}

void StreamBufferCache::removeStreamsExcept(std::set<int32_t> streams_to_keep) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = cache_.begin(); it != cache_.end();) {
    if (streams_to_keep.count((*it)->streamId()) == 0) {
      it = cache_.erase(it);
    } else {
      it++;
    }
  }
}

}  // namespace android::hardware::camera::device::V3_4::implementation
