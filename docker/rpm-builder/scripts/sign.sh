#!/usr/bin/env bash

# It signs all RPM packages in the `x64_86` directory.
RPMS="${HOME}/${REPO_NAME}/tools/rpmbuild/RPMS/x86_64"
for f in "${RPMS}"/*.rpm; do
  ./rpm-sign.exp "$f" "$PGP_PASSPHRASE"
done
