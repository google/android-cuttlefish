#!/usr/bin/env bash
exec "$(dirname "$0")/presubmit_podcvd_e2etest.sh" -c podcvd_with_gpu
