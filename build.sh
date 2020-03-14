#!/bin/bash -r
docker build -t cuttlefish . --build-arg UID=$UID
