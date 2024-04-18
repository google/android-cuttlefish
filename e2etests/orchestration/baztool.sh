#!/bin/bash

echo  "TEST_SRCDIR: ${TEST_SRCDIR}"

echo "BazTool"

echo "Generated from BazTool: $@" > /tmp/generatedfrombaztool.txt

echo "something" > "$@"


