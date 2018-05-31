#!/usr/bin/env python
#
# Copyright (C) 2018 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0(the "License");
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
#
"""Queries the capabilities of the cuttlefish-* packages.

Some cuttlefish functionalities are not supported on all host packages versions.
This script allows to query for support of specific functionalities instead of
comparing the version numbers.
"""

import sys

def main():
    capabilities = {"capability_check"}
    if len(sys.argv) == 1:
        # Print all capabilities
        print('\n'.join(capabilities))
    else:
        # Exit non-zero if any of the capabilities are not supported
        query = set(sys.argv[1:])
        sys.exit(len(query - capabilities))


if __name__ == '__main__':
    main()
