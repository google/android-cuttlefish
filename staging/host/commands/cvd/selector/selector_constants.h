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

// group_name + "-" + per_instance_name
constexpr char kDeviceNameField[] = "device_name";
constexpr char kGroupNameField[] = "group_name";
constexpr char kHomeField[] = "home";
constexpr char kInstanceIdField[] = "instance_id";

/* per_instance_name
 *
 * by default, to_string(instance_id), and users can override it
 */
constexpr char kInstanceNameField[] = "instance_name";

/**
 * this is used not by instance db but by selector front-end
 */
constexpr char kNameOpt[] = "name";

}  // namespace selector
}  // namespace cuttlefish
