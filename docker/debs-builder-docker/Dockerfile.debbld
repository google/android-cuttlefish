# This file is based on https://hub.docker.com/r/jrei/systemd-debian/.

# take the same debian image as in .github/test_build_deb.yaml
FROM debian:stable-20211011 AS cuttlefish-hostpkg-builder

ENV container docker
ENV LC_ALL C
ENV DEBIAN_FRONTEND noninteractive

# Set up the user have the same UID as user creating the container.  This is
# important when we map the (container) user's home directory as a docker volume.

ARG UID

USER root
WORKDIR /root

RUN set -x

RUN apt-get update

VOLUME [ "/sys/fs/cgroup" ]

RUN apt-get update \
    && apt-get install --no-install-recommends -y apt-utils sudo vim dpkg-dev devscripts gawk coreutils equivs git curl

SHELL ["/bin/bash", "-c"]

RUN useradd -ms /bin/bash vsoc-01 -d /home/vsoc-01 -u $UID \
    && passwd -d vsoc-01 \
    && echo 'vsoc-01 ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

RUN mkdir -p /home/vsoc-01/host/android-cuttlefish && chown -R vsoc-01 /home/vsoc-01
