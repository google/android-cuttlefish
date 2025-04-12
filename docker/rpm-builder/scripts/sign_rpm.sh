#!/usr/bin/env bash

# Check if the environmental variables were set.
if [ -z ${RPM_SIGN_SIGNATURE+x} ]; then echo "RPM_SIGN_SIGNATURE is unset"; else echo "RPM_SIGN_SIGNATURE is set"; fi
if [ -z ${RPM_SIGN_PASSPHRASE+x} ]; then echo "RPM_SIGN_PASSPHRASE is unset"; else echo "RPM_SIGN_PASSPHRASE is set"; fi

# It signs all packages below the `rpmbuild/RPMS` directory.
REPO_DIR="$(realpath "$(dirname "$0")/../..")"
RPMS="${REPO_DIR}/tools/rpmbuild/RPMS/*/*.rpm"

for FILE in $RPMS; do
  rpm_sign.exp "${FILE}" "${RPM_SIGN_PASSPHRASE}"
done
