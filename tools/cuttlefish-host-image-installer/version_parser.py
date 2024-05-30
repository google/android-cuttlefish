#!/usr/bin/python3
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
"""Parse changelog and get either latest version or stable version.

"""

import os.path
import re
import sys

def main():
    stable_version = False
    if len(sys.argv) < 2 or len(sys.argv) > 3:
        # Exit non-zero if input parameter format is wrong
        print("""Input format is wrong.
usage: version_parser.py <changelog> [latest|stable]
return the corresponding version and write the corresponding changelog context in current directory""")
        sys.exit(1)
    elif len(sys.argv) == 3:
        # 2nd input parameter is optional
        # latest or stable, default is latest
        if sys.argv[2].lower() == 'stable':
            stable_version = True

    changelog = sys.argv[1]
    if not os.path.isfile(changelog):
        print("changelog {} does not exists".format(changelog))
        sys.exit(1)

    file1 = open(changelog, 'r')
    Lines = file1.readlines()

    stable_version_list = []
    version_list = []

    context = ""
    previous_version_stable = -1 # 0: unstable, 1: stable, -1: start state, no previous version
    for line in Lines:
        # parse cuttlefish-common/cuttlefish-frontend changelog
        # sample line with keywords: cuttlefish-frontend (0.9.28) stable; urgency=medium
        tokens = re.search(r'.*cuttlefish-[a-zA-Z]+ \((?P<version>.*)\) (?P<status>[a-zA-Z]+); .*', line)

        if tokens:
            version = tokens.group('version')
            status = tokens.group('status')
            if previous_version_stable == 0:
                last_item = version_list.pop()
                version_list.append((last_item, context))
            elif previous_version_stable == 1:
                last_item = stable_version_list.pop()
                stable_version_list.append((last_item, context))
            if status.lower() == 'stable':
                stable_version_list.append(version)
                previous_version_stable = 1
            else:
                version_list.append(version)
                previous_version_stable = 0
            context = ""
        context = context + line

    changelog = ""
    if stable_version:
        print(stable_version_list[0][0])
        changelog = stable_version_list[0][1]
    else:
        print(version_list[0][0])
        changelog = version_list[0][1]
    with open("changelog", "a") as myfile:
        myfile.write(changelog)


if __name__ == '__main__':
    main()
