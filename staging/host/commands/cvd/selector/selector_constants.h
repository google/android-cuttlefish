/*
 * Copyright (C) 2022 The Android Open Source Project
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

namespace cuttlefish {
namespace selector {

// The name of environment variable that points to the host out directory
constexpr char kAndroidHostOut[] = "ANDROID_HOST_OUT";

/*
 * These are fields in instance database
 *
 */
constexpr char kGroupNameField[] = "group_name";
constexpr char kHomeField[] = "home";
constexpr char kInstanceIdField[] = "instance_id";
/* per_instance_name
 *
 * by default, to_string(instance_id), and users can override it
 */
constexpr char kInstanceNameField[] = "instance_name";

/**
 * these are used not by instance db but by selector front-end
 *
 * E.g. --name, --device_name, -group_name, etc
 *
 * A device name is group name followed by "-" followed by a per-
 * instance name (or, interchangeably, instance_name).
 *
 * E.g. "cvd-1" could be a device name, "cvd" being the group name,
 * and "1" being the instance name.
 *
 */
constexpr char kNameOpt[] = "name";
// device_name == (group_name + "-" + instance_name)
constexpr char kDeviceNameOpt[] = "device_name";
constexpr char kGroupNameOpt[] = "group_name";
constexpr char kInstanceNameOpt[] = "instance_name";

}  // namespace selector
}  // namespace cuttlefish
