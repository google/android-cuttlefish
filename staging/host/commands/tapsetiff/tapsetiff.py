#!/usr/bin/python

# Copyright (C) 2020 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import fcntl
import struct
import sys

TUNSETIFF = 0x400454ca
IFF_TAP = 0x0002
IFF_NO_PI = 0x1000
IFF_VNET_HDR = 0x4000

tun_fd = int(sys.argv[1])
tap_name = sys.argv[2]

ifr = struct.pack('16sH', tap_name, IFF_TAP | IFF_NO_PI | IFF_VNET_HDR)
fcntl.ioctl(tun_fd, TUNSETIFF, ifr)
