#!/system/bin/sh
# Copyright (C) 2024 The Android Open Source Project
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

# Create a namespace specifically for auto ethernet usage.
ip netns add auto_eth

# Move network interface eth1 to network namespace auto_eth
# Once moved, we no longer be able to see the network interface eth1 here
# without entering auto_eth network namespace
ip link set eth1 netns auto_eth