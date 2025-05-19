/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include <charconv>
#include <condition_variable>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <android-base/macros.h>

#include "adb.h"
#include "adb_unique_fd.h"

void close_stdin();

bool getcwd(std::string* cwd);
bool directory_exists(const std::string& path);

// Return the user's home directory.
std::string adb_get_homedir_path();

// Return the adb user directory.
std::string adb_get_android_dir_path();

bool mkdirs(const std::string& path);

std::string escape_arg(const std::string& s);

std::string dump_hex(const void* ptr, size_t byte_count);
std::string dump_header(const amessage* msg);
std::string dump_packet(const char* name, const char* func, const apacket* p);

std::string perror_str(const char* msg);

bool set_file_block_mode(borrowed_fd fd, bool block);

// Given forward/reverse targets, returns true if they look valid. If an error is found, fills
// |error| and returns false.
// Currently this only checks "tcp:" targets. Additional checking could be added for other targets
// if needed.
bool forward_targets_are_valid(const std::string& source, const std::string& dest,
                               std::string* error);

// A thread-safe blocking queue.
template <typename T>
class BlockingQueue {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<T> queue;

  public:
    void Push(const T& t) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            queue.push_back(t);
        }
        cv.notify_one();
    }

    template <typename Fn>
    void PopAll(Fn fn) {
        std::vector<T> popped;

        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this]() { return !queue.empty(); });
            popped = std::move(queue);
            queue.clear();
        }

        for (const T& t : popped) {
            fn(t);
        }
    }
};

std::string GetLogFilePath();

inline std::string_view StripTrailingNulls(std::string_view str) {
    size_t n = 0;
    for (auto it = str.rbegin(); it != str.rend(); ++it) {
        if (*it != '\0') {
            break;
        }
        ++n;
    }

    str.remove_suffix(n);
    return str;
}

// Base-10 stroll on a string_view.
template <typename T>
inline bool ParseUint(T* result, std::string_view str, std::string_view* remaining = nullptr) {
    T value;
    const auto res = std::from_chars(str.begin(), str.end(), value);
    if (res.ec != std::errc{}) {
        return false;
    }
    if (res.ptr != str.end() && !remaining) {
        return false;
    }
    if (remaining) {
        *remaining = std::string_view(res.ptr, str.end() - res.ptr);
    }
    *result = value;
    return true;
}
