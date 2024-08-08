#!/bin/bash
. ./setup-nodejs-env.sh
install_nodejs
package_version=$(head -1 debian/changelog | sed s/.*\(\\\(.*\\\)\).*/\\1/)
last_commit=$( (git log -1 || echo dev) | head -1 | sed s/commit\ //)
echo "export const BUILD_VERSION = \"github-$package_version-$last_commit\";" > src/operator/webui/src/environments/version.ts
(cd src/operator/webui/ && npm install && ./node_modules/.bin/ng build)
ok=$?
uninstall_nodejs
exit $ok
