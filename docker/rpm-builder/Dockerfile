# Dockerfile based on Rocky Linux
# https://hub.docker.com/_/rockylinux
# docker build --target builder -t syslogic/rpm-builder:latest .
# docker build --target integration -t syslogic/rpm-integration:latest .
# docker build --target orchestration -t syslogic/rpm-orchestration:latest .
ARG ROCKY_LINUX_VERSION=9.5
FROM rockylinux/rockylinux:$ROCKY_LINUX_VERSION AS rockylinux
LABEL description="RPM Builder" version="1.0.0" repository="https://github.com/google/android-cuttlefish" maintainer="Martin Zeitler"
ENTRYPOINT [ "/bin/bash" ]
VOLUME [ "/root" ]

FROM rockylinux AS builder
ARG BAZEL_VERSION=7.6.0

# Install build dependencies.
# RUN dnf -y swap curl-minimal curl-full
# RUN dnf -y swap coreutils-single coreutils
# RUN dnf -y install dpkg-dev devscripts vim-enhanced
# RUN dnf -y install rpm-devel rpmdevtools rpmlint gcc make python diffutils patch

# EPEL repository provides `ncurses-compat-libs` and installs `dnf-core-plugin`, which has `dnf builddeps`.
RUN dnf -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm && dnf -y upgrade
# Provide build dependencies.
RUN dnf -y install git sudo rpm-build golang-bin curl-devel wayland-devel ncurses-compat-libs
# Package `protobuf` comes from CRB repository
RUN dnf -y install protobuf protobuf-devel protobuf-compiler --enablerepo crb
RUN dnf clean all

# Add entrypoint scripts.
ADD ./docker/rpm-builder/buildscript.sh /root/buildscript.sh
ADD ./docker/rpm-builder/testscript.sh /root/testscript.sh
ENTRYPOINT [ "/bin/bash", "-c", "/root/buildscript.sh" ]

# Modify file `~/.bash_profile` and install `bazelisk`.
RUN echo "USE_BAZEL_VERSION=${BAZEL_VERSION}" >> ~/.bazeliskrc
RUN echo "export PATH=${PATH}:${HOME}/go/bin" >> ~/.bash_profile
RUN echo "export GOPATH=${HOME}/go" >> ~/.bash_profile
RUN source ~/.bash_profile
RUN go install github.com/bazelbuild/bazelisk@latest


# Integration dependencies.
FROM builder AS integration
# Expose Operator Port (HTTP:1080, HTTPS:1443)
EXPOSE 1080 1443
# Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
EXPOSE 2080 2443
# Expose WebRTC
EXPOSE 8443/tcp 15550-15560
# Expose ADB
EXPOSE 6520-6620

RUN dnf -y install qemu-kvm
RUN dnf clean all

# Orchestration dependencies.
FROM builder AS orchestration
# Expose Operator Port (HTTP:1080, HTTPS:1443)
EXPOSE 1080 1443
# Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
EXPOSE 2080 2443
# Expose WebRTC
EXPOSE 8443/tcp 15550-15560
# Expose ADB
EXPOSE 6520-6620

RUN dnf -y install nginx systemd-journal-remote
RUN dnf clean all