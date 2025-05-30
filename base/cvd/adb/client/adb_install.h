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

#include <string>
#include <string_view>

// secure dex metadata, for cloud compilation
constexpr std::string_view kSdmExtension = ".sdm";
// dex metadata, for cloud profile and cloud verification
constexpr std::string_view kDmExtension = ".dm";

int install_app(int argc, const char** argv);
int install_multiple_app(int argc, const char** argv);
int install_multi_package(int argc, const char** argv);
int uninstall_app(int argc, const char** argv);

int delete_device_file(const std::string& filename);
int delete_host_file(const std::string& filename);

