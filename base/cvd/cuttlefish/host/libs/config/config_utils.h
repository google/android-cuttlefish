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

namespace cuttlefish {

// Returns the instance number as obtained from the
// *kCuttlefishInstanceEnvVarName environment variable or the username.
int GetInstance();

// Returns default Vsock CID, which is
// GetInstance() + 2
int GetDefaultVsockCid();

// Calculates vsock server port number
// return base + (vsock_guest_cid - 3)
int GetVsockServerPort(const int base,
                       const int vsock_guest_cid);

// Returns a path where the launcher puts a link to the config file which makes
// it easily discoverable regardless of what vm manager is in use
std::string GetGlobalConfigFileLink();

// This function modifies a given base value to make it different across
// different instances by appending the instance id in case of strings or adding
// it in case of integers.
std::string ForCurrentInstance(const char* prefix);

int InstanceFromString(std::string instance_str);

// Returns a random serial number appended to a given prefix.
std::string RandomSerialNumber(const std::string& prefix);

std::string DefaultHostArtifactsPath(const std::string& file);
std::string DefaultQemuBinaryDir();
std::string HostBinaryPath(const std::string& binary_name);
std::string HostUsrSharePath(const std::string& file);
std::string HostQemuBiosPath();
std::string DefaultGuestImagePath(const std::string& file);
std::string DefaultEnvironmentPath(const std::string& environment_key,
                                   const std::string& default_value,
                                   const std::string& subpath);
bool IsValidAndroidHostOutPath(const std::string& path);

// Whether the host supports qemu
bool HostSupportsQemuCli();

// Whether to use our local QEMU prebuilt
bool UseQemuPrebuilt();

std::string GetSeccompPolicyDir();

}  // namespace cuttlefish
