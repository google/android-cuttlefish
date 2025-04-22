/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <inttypes.h>
#include <stdlib.h>

#include <string>
#include <vector>

#include <android-base/unique_fd.h>
#include <bootimg.h>
#include <liblp/liblp.h>
#include <sparse/sparse.h>

using SparsePtr = std::unique_ptr<sparse_file, decltype(&sparse_file_destroy)>;

/* util stuff */
double now();
void set_verbose();

// These printf-like functions are implemented in terms of vsnprintf, so they
// use the same attribute for compile-time format string checking.
void die(const char* fmt, ...) __attribute__((__noreturn__))
__attribute__((__format__(__printf__, 1, 2)));

void verbose(const char* fmt, ...) __attribute__((__format__(__printf__, 1, 2)));

void die(const std::string& str) __attribute__((__noreturn__));

bool should_flash_in_userspace(const android::fs_mgr::LpMetadata& metadata,
                               const std::string& partition_name);
bool is_sparse_file(android::base::borrowed_fd fd);
int64_t get_file_size(android::base::borrowed_fd fd);
std::string fb_fix_numeric_var(std::string var);

class ImageSource {
  public:
    virtual ~ImageSource(){};
    virtual bool ReadFile(const std::string& name, std::vector<char>* out) const = 0;
    virtual android::base::unique_fd OpenFile(const std::string& name) const = 0;
};
