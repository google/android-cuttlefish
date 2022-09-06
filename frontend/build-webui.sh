#!/bin/bash
. ./setup-nodejs-env.sh
install_nodejs
(cd src/operator/webui/ && npm install && ./node_modules/.bin/ng build)
uninstall_nodejs
