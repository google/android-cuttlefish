#!/usr/bin/env bash

sed -E 's|[^[:alnum:]]\[[^m]+m||g' "$@"
