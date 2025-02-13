// Copyright 2022 The Android Open Source Project
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

#pragma once

#include <list>
#include <unordered_map>

template <typename Key, typename Value>
class LruCache {
 public:
  LruCache(std::size_t maxSize) : m_maxSize(maxSize) {
    m_table.reserve(maxSize);
  }

  Value* get(const Key& key) {
    auto tableIt = m_table.find(key);
    if (tableIt == m_table.end()) {
      return nullptr;
    }

    // Move to front.
    auto elementsIt = tableIt->second;
    m_elements.splice(elementsIt, m_elements, m_elements.begin());
    return &elementsIt->value;
  }

  void set(const Key& key, Value&& value) {
    auto tableIt = m_table.find(key);
    if (tableIt == m_table.end()) {
      if (m_table.size() >= m_maxSize) {
        auto& kv = m_elements.back();
        m_table.erase(kv.key);
        m_elements.pop_back();
      }
    } else {
      auto elementsIt = tableIt->second;
      m_elements.erase(elementsIt);
    }
    m_elements.emplace_front(KeyValue{
        key,
        std::forward<Value>(value),
    });
    m_table[key] = m_elements.begin();
  }

  void remove(const Key& key) {
    auto tableIt = m_table.find(key);
    if (tableIt == m_table.end()) {
      return;
    }
    auto elementsIt = tableIt->second;
    m_elements.erase(elementsIt);
    m_table.erase(tableIt);
  }

  void clear() {
    m_elements.clear();
    m_table.clear();
  }

 private:
  struct KeyValue {
    Key key;
    Value value;
  };

  const std::size_t m_maxSize;
  // Front is the most recently used and back is the least recently used.
  std::list<KeyValue> m_elements;
  std::unordered_map<Key, typename std::list<KeyValue>::iterator> m_table;
};
