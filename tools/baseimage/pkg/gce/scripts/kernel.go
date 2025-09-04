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

// https://cs.android.com/android/platform/superproject/main/+/main:device/google/cuttlefish/tools/update_gce_kernel.sh;drc=7f601ad9132960b58ee3d7fe8f8b382d20720a22
const UpdateKernel = `#!/usr/bin/env bash
set -o errexit -o nounset -o pipefail

sudo apt update
sudo apt install -t bookworm -y linux-image-cloud-amd64
sudo reboot
`

// https://cs.android.com/android/platform/superproject/main/+/main:device/google/cuttlefish/tools/update_gce_kernel.sh;drc=7f601ad9132960b58ee3d7fe8f8b382d20720a22
const RemoveOldKernel = `#!/usr/bin/env bash
set -o errexit -o nounset -o pipefail

dpkg --list | grep -v $(uname -r) | grep -E 'linux-image-[0-9]|linux-headers-[0-9]' | awk '{print $2" "$3}' | sort -k2,2 | awk '{print $1}' | xargs sudo apt-get -y purge
sudo update-grub2
`
