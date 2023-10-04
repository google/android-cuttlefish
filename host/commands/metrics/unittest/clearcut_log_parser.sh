#!/bin/bash
#
# Copyright (C) 2023 The Android Open Source Project
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
#

# This function searches for log files within the specified directory
# that contain the given string in their filenames.
# It returns a list of matching log file paths.
FindLogFiles() {
    # Check for the correct number of arguments
    if [[ "$#" -ne 2 ]]; then
        echo "Usage: ${FUNCNAME} <directory> <string>"
        return 1
    fi

    # Get the directory and string from the arguments
    local directory="$1"
    local string="$2"

    # Ensure the directory exists
    if [[ ! -d "$directory" ]]; then
        echo "Error: Directory $directory does not exist."
        return 1
    fi

    # Use the find command to search for files with names containing the given string
    find "$directory" -type f -name "*$string*"
}

# /tmp/clearcut-logwriter is the default directory where clearcut logs are stored
logs_directory="/tmp/clearcut-logwriter"

# Find log files for "cuttlefish" metrics
cuttlefish_logs=$(FindLogFiles "$logs_directory" "cuttlefish")
echo "$cuttlefish_logs"

# Find log files for "atest" internal metrics
atest_internal_logs=$(FindLogFiles "$logs_directory" "atest_internal")
echo "$atest_internal_logs"

# Find log files for "atest" external metrics
atest_external_logs=$(FindLogFiles "$logs_directory" "atest_external")
echo "$atest_external_logs"

# gqui is a tool to parse clearcut logs
gqui from "$cuttlefish_logs" proto GWSLogEntryProto > clearcut_cf.txt 2> clearcut_cf_error.txt
gqui from "$atest_internal_logs" proto GWSLogEntryProto > clearcut_atest_internal.txt 2> clearcut_atest_internal_error.txt
gqui from "$atest_external_logs" proto GWSLogEntryProto > clearcut_atest_external.txt 2> clearcut_atest_external_error.txt
