#!/bin/bash

# Fail on errors
set -e -x

function test_results() {
        echo "Cuttlefish debian package build and testing placeholder script for kokoro"

        echo "current directory:"
        pwd
        echo "Current directory stats"
        stat .

        echo "contents of github/android-cuttlefish"
        ls -l github/android-cuttlefish

        echo "producing placeholder xUnit test results"
        cp github/android-cuttlefish/.kokoro/demo_result.xml "$1"

        echo "Current user: $(whoami)"
        echo "Current groups: $(groups)"

        echo "done"
        sudo echo "running as root?"
        sudo whoami
}

function run_test() {
  test_name="$1"
  shift
  # Run the test function with:
  # The xml output file as first parameter
  # The other arguments passed to this function
  # stderr and stdout redirected to the log file
  mkdir -p "${test_name}"
  "${test_name}" \
    "${test_name}/sponge_log.xml" \
    "$@" \
    1>&2 2>"${test_name}/sponge_log.log"
}

run_test test_results

cat >test_results/sponge_log.xml <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<testsuites tests="1" failures="0" disabled="0" errors="0" time="0" timestamp="2024-02-23T23:56:49.153" name="AllTests">
  <testsuite name="HelloTest" tests="1" failures="0" disabled="0" skipped="0" errors="0" time="0" timestamp="2024-02-23T23:56:49.153">
    <testcase name="BasicAssertions" file="hello_test.cc" line="4" status="run" result="completed" time="0" timestamp="2024-02-23T23:56:49.153" classname="HelloTest" />
  </testsuite>
</testsuites>
EOF

