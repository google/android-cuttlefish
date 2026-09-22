#!/usr/bin/env bash      
exec "$(dirname "$0")/presubmit_cvd_e2etest.sh" -i 3 -n 8
