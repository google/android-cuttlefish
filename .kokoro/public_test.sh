#!/bin/bash

# Fail on errors
set -e

echo "Cuttlefish debian package build and testing placeholder script for kokoro"

echo "current directory:"
pwd

echo "contents of github/android-cuttlefish"
ls github/android-cuttlefish

echo "producing placeholder xUnit test results"
mkdir -p test_results
cp github/android-cuttlefish/.kokoro/demo_result.xml test_results/sponge_log.xml
echo "placeholder log" > test_results/sponge_log.log

echo "done"
