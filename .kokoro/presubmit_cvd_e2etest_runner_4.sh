#!/usr/bin/env bash      
exec "$(dirname "$0")/presubmit_cvd_e2etest.sh" -i 4 -n 8
