#!/usr/bin/env bash

# Copyright (C) 2025 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -o errexit -o nounset -o pipefail

sudo apt update

sudo apt install -y cloud-utils
sudo apt install -y cloud-guest-utils
sudo apt install -y fdisk
sudo growpart /dev/sdb 1 || /bin/true
sudo e2fsck -f -y /dev/sdb1 || /bin/true
sudo resize2fs /dev/sdb1
