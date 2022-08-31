#!/bin/bash
. ./setup-nodejs-env.sh
./install-nodejs.sh
(cd src/operator/webui/ && npm install && ./node_modules/.bin/ng build)
