#!/usr/bin/env bash

# It signs all packages below the `rpmbuild/RPMS` directory.
REPO_DIR="$(realpath "$(dirname "$0")/../..")"
RPMS="${REPO_DIR}/tools/rpmbuild/RPMS/*/*.rpm"

for file in $RPMS; do
  ./rpm_sign.exp "$file" "$PGP_PASSPHRASE"
done
