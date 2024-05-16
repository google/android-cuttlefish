#!/bin/bash

cd "$(dirname "$0")"
cd ..

codespell --skip=tools/codespell-ignore.txt -I tools/codespell-ignore.txt -w -i 3
