#!/bin/bash -r
set -o errexit
name=${1:-cuttlefish}

if [ -n "$(docker ps -q -a -f name=${name})" ]; then
  homedir=$(docker inspect -f '{{range $mount:=.Mounts}}{{if and (eq .Destination "/home/vsoc-01") (eq .Type "bind")}}{{- printf "%q" $mount.Source}}{{end}}{{end}}' $name | sed 's/"//g')
  echo "Deleting container $name."
  docker rm -f $name
  echo "Cleaning up homedir $homedir."
  rm -rf $homedir
else
  echo "Nothing to stop: container ${name} does not exist."
fi
