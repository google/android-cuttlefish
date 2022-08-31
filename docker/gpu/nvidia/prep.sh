#!/bin/bash

apt install --no-install-recommends -y software-properties-common
apt install --no-install-recommends -y equivs

apt-add-repository 'deb http://deb.debian.org/debian buster-backports main contrib non-free'
#apt-add-repository contrib
#apt-add-repository non-free
apt update
