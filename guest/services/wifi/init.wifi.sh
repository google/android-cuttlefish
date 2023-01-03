#!/vendor/bin/sh

# Copyright 2021 Google Inc. All rights reserved.

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

wifi_mac_prefix=`getprop ro.boot.wifi_mac_prefix`
if [ -n "$wifi_mac_prefix" ]; then
    /vendor/bin/mac80211_create_radios --enable-pmsr 2 $wifi_mac_prefix || exit 1
fi

