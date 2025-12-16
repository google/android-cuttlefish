// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package scripts

import _ "embed"

//go:embed mount_attached_disk.sh
var MountAttachedDisk string

//go:embed create_base_image_main.sh
var CreateBaseImageMain string

//go:embed install_nvidia.sh
var InstallNvidia string

//go:embed fill_available_disk_space.sh
var FillAvailableDiskSpace string

//go:embed install_cuttlefish_debs.sh
var InstallCuttlefishPackages string

//go:embed install_kernel_main.sh
var InstallKernelMain string

//go:embed validate_cuttlefish_image.sh
var ValidateCuttlefishImage string
