/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <sys/types.h>

#include <chrono>
#include <string>

namespace cvd {
bool FileExists(const std::string& path);
bool FileHasContent(const std::string& path);
bool DirectoryExists(const std::string& path);
bool IsDirectoryEmpty(const std::string& path);
off_t FileSize(const std::string& path);
bool RemoveFile(const std::string& file);
std::chrono::system_clock::time_point FileModificationTime(const std::string& path);

// The returned value may contain .. or . if these are present in the path
// argument.
// path must not contain ~
std::string AbsolutePath(const std::string& path);

std::string CurrentDirectory();
}  // namespace cvd
