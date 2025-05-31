/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <sys/cdefs.h>

#include <chrono>
#include <limits>
#include <mutex>
#include <optional>
#include <string>

struct prop_info;

namespace android {
namespace base {

// Returns the current value of the system property `key`,
// or `default_value` if the property is empty or doesn't exist.
std::string GetProperty(const std::string& key, const std::string& default_value);

// Returns true if the system property `key` has the value "1", "y", "yes", "on", or "true",
// false for "0", "n", "no", "off", or "false", or `default_value` otherwise.
bool GetBoolProperty(const std::string& key, bool default_value);

// Returns the signed integer corresponding to the system property `key`.
// If the property is empty, doesn't exist, doesn't have an integer value, or is outside
// the optional bounds, returns `default_value`.
template <typename T> T GetIntProperty(const std::string& key,
                                       T default_value,
                                       T min = std::numeric_limits<T>::min(),
                                       T max = std::numeric_limits<T>::max());

// Returns the unsigned integer corresponding to the system property `key`.
// If the property is empty, doesn't exist, doesn't have an integer value, or is outside
// the optional bound, returns `default_value`.
template <typename T> T GetUintProperty(const std::string& key,
                                        T default_value,
                                        T max = std::numeric_limits<T>::max());

// Sets the system property `key` to `value`.
bool SetProperty(const std::string& key, const std::string& value);

// Waits for the system property `key` to have the value `expected_value`.
// Times out after `relative_timeout`.
// Returns true on success, false on timeout.
#if defined(__BIONIC__)
bool WaitForProperty(const std::string& key, const std::string& expected_value,
                     std::chrono::milliseconds relative_timeout = std::chrono::milliseconds::max());
#endif

// Waits for the system property `key` to be created.
// Times out after `relative_timeout`.
// Returns true on success, false on timeout.
#if defined(__BIONIC__)
bool WaitForPropertyCreation(const std::string& key, std::chrono::milliseconds relative_timeout =
                                                         std::chrono::milliseconds::max());
#endif

#if defined(__BIONIC__) && __cplusplus >= 201703L
// Cached system property lookup. For code that needs to read the same property multiple times,
// this class helps optimize those lookups.
class CachedProperty {
 public:
  explicit CachedProperty(std::string property_name);

  // Kept for ABI compatibility.
  explicit CachedProperty(const char* property_name);

  // Returns the current value of the underlying system property as cheaply as possible.
  // The returned pointer is valid until the next call to Get. Because most callers are going
  // to want to parse the string returned here and cache that as well, this function performs
  // no locking, and is completely thread unsafe. It is the caller's responsibility to provide a
  // lock for thread-safety.
  //
  // Note: *changed can be set to true even if the contents of the property remain the same.
  const char* Get(bool* changed = nullptr);

  // Waits for the property to be changed and then reads its value.
  // Times out returning nullptr, after `relative_timeout`
  //
  // Note: this can return the same value multiple times in a row if the property was set to the
  // same value or if multiple changes happened before the current thread was resumed.
  const char* WaitForChange(
      std::chrono::milliseconds relative_timeout = std::chrono::milliseconds::max());

 private:
  std::string property_name_;
  const prop_info* prop_info_;
  std::optional<uint32_t> cached_area_serial_;
  std::optional<uint32_t> cached_property_serial_;
  char cached_value_[92];
  bool is_read_only_;
  const char* read_only_property_;
};

// Helper class for passing the output of CachedProperty to a parser function, and then caching
// that as well.
template <typename Parser>
class CachedParsedProperty {
 public:
  using value_type = std::remove_reference_t<std::invoke_result_t<Parser, const char*>>;

  CachedParsedProperty(std::string property_name, Parser parser)
      : cached_property_(std::move(property_name)), parser_(std::move(parser)) {}

  // Returns the parsed value.
  // This function is internally-synchronized, so use from multiple threads is safe (but ordering
  // of course cannot be guaranteed without external synchronization).
  value_type Get(bool* changed = nullptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool local_changed = false;
    const char* value = cached_property_.Get(&local_changed);
    if (!cached_result_ || local_changed) {
      cached_result_ = parser_(value);
    }

    if (changed) *changed = local_changed;
    return *cached_result_;
  }

 private:
  std::mutex mutex_;
  CachedProperty cached_property_;
  std::optional<value_type> cached_result_;

  Parser parser_;
};

// Helper for CachedParsedProperty that uses android::base::ParseBool.
class CachedBoolProperty {
 public:
  explicit CachedBoolProperty(std::string property_name);

  // Returns the parsed bool, or std::nullopt if it wasn't set or couldn't be parsed.
  std::optional<bool> GetOptional();

  // Returns the parsed bool, or default_value if it wasn't set or couldn't be parsed.
  bool Get(bool default_value);

 private:
  CachedParsedProperty<std::optional<bool> (*)(const char*)> cached_parsed_property_;
};

#endif

static inline int HwTimeoutMultiplier() {
  return android::base::GetIntProperty("ro.hw_timeout_multiplier", 1);
}

} // namespace base
} // namespace android

#if !defined(__BIONIC__)
/** Implementation detail. */
extern "C" int __system_property_set(const char*, const char*);
/** Implementation detail. */
extern "C" int __system_property_get(const char*, char*);
/** Implementation detail. */
extern "C" const prop_info* __system_property_find(const char*);
/** Implementation detail. */
extern "C" void __system_property_read_callback(const prop_info*,
                                                void (*)(void*, const char*, const char*, uint32_t),
                                                void*);
#endif
