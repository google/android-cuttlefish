#!/usr/bin/env bash      
exec "$(dirname "$0")/presubmit_cvd_e2etest.sh" -c cvd_with_gpu
