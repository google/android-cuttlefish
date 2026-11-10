#!/bin/bash
#
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

# Starts each host binary once and checks that it runs to a controlled
# exit. This catches what compiling and linking cannot: the dynamic loader
# failing to resolve a library, static initializers crashing, or flag
# registration failing before main runs. Help and version output are used
# because they need no device, config or network.
#
# Binaries that CHECK on a missing cuttlefish config before parsing flags
# are not listed, since a CHECK failure aborts the same way a crash does.

set -u

# Keep cvd from reading or writing the real home directory.
HOME=${TEST_TMPDIR:?}
export HOME

failed=0

# check <binary> <args...>: run and require output and an exit code that
# is not a signal. Help exits 1 under gflags, so 0 is not required.
check() {
  local output
  output=$("$@" </dev/null 2>&1)
  local status=$?
  if [[ ${status} -ge 128 || -z ${output} ]]; then
    echo "FAIL: $* exited ${status}"
    echo "${output}" | tail -n 20
    failed=1
  else
    echo "ok: $* exited ${status}"
  fi
}

check "${CVD:?}" version
check "${CVD}" help
check "${ASSEMBLE_CVD:?}" --help
check "${RUN_CVD:?}" --version
check "${ECHO_SERVER:?}" --version
check "${SCREEN_RECORDING_SERVER:?}" --version
check "${CVD_INTERNAL_STATUS:?}" --help
check "${CVD_INTERNAL_STOP:?}" --help

exit "${failed}"
