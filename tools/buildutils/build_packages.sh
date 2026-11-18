#!/usr/bin/env bash

set -e -x

REPO_DIR="$(realpath "$(dirname "$0")/../..")"
SETUP_BUILD_ENV="$(dirname $0)/setup_build_env.sh"
BUILD_PACKAGE="$(dirname $0)/build_package.sh"

"${SETUP_BUILD_ENV}"

# "${BUILD_PACKAGE}" "${REPO_DIR}/container" $@
"${BUILD_PACKAGE}" "${REPO_DIR}/base" $@
"${BUILD_PACKAGE}" "${REPO_DIR}/frontend" $@
