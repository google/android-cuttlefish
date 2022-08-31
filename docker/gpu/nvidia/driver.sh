#!/bin/bash

# $1 = filter script

script=$1

# echo nvidia-driver | while read -e stem version; do
dpkg-query -f='${Package} ${Version}\n' -W '*nvidia*' | while read -e stem version; do
  if [ -n "${script}" ]; then
    if "${script}" _ _ ${stem} _ _ _ _; then
      echo "${stem} ${version}"
    fi
  else
      echo "${stem} ${version}"
  fi
done
