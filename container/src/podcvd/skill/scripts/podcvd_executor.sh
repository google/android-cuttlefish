#!/bin/bash

set -e

SID=$(ps -o sid= -p $$ | xargs)
PODCVD_CLIENT_ID="${SID}-$(stat -c '%Y' "/proc/${SID}")" podcvd "$@"
