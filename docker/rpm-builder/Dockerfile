# Dockerfile based on Rocky Linux
# https://hub.docker.com/_/rockylinux
# docker build --target builder -t syslogic/rpm-builder:latest .
# docker build --target integration -t syslogic/rpm-integration:latest .
# docker build --target orchestration -t syslogic/rpm-orchestration:latest .
ARG ROCKY_LINUX_VERSION=9.5
FROM rockylinux/rockylinux:$ROCKY_LINUX_VERSION AS rockylinux
LABEL description="RPM Builder" version="1.0.0" repository="https://github.com/google/android-cuttlefish" maintainer="Martin Zeitler"

# Base Image
FROM rockylinux AS base

# EPEL repository provides `ncurses-compat-libs` and installs `dnf-core-plugin`, which has `dnf builddeps`.
RUN dnf -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm && dnf -y upgrade && dnf clean all

# RPM Builder
FROM base AS builder
ARG BAZEL_VERSION=7.6.0

# Add entrypoint script.
ADD ./docker/rpm-builder/buildscript.sh /root/entrypoint.sh
ENTRYPOINT [ "/bin/bash", "-c", "/root/entrypoint.sh" ]

# Build dependencies.
# RUN dnf -y install dpkg-dev devscripts vim-enhanced
# RUN dnf -y install rpm-devel rpmdevtools rpmlint gcc make python diffutils patch
RUN dnf -y install git sudo rpm-build golang-bin curl-devel wayland-devel ncurses-compat-libs \
# Package `protobuf` comes from CRB repository
protobuf protobuf-devel protobuf-compiler --enablerepo crb && dnf clean all

# Modify file `~/.bash_profile`,
RUN echo "export GOPATH=${HOME}/go" >> ~/.bash_profile
RUN echo "export PATH=${PATH}:${HOME}/go/bin" >> ~/.bash_profile
RUN source ~/.bash_profile

# Go install `bazelisk`.
RUN echo "USE_BAZEL_VERSION=${BAZEL_VERSION}" >> ~/.bazeliskrc
RUN go install github.com/bazelbuild/bazelisk@latest


# RPM Testing
# Now using `FROM base` ...
FROM base AS testing

# Add entrypoint script.
ADD ./docker/rpm-builder/testscript.sh /root/entrypoint.sh
ENTRYPOINT [ "/bin/bash", "-c", "/root/entrypoint.sh" ]

# Runtime Dependencies
RUN dnf -y install libwayland-* && dnf clean all


# RHEL Integration
FROM base AS integration
ENTRYPOINT [ "/bin/bash" ]

# Runtime Dependencies
RUN dnf -y install qemu-kvm && dnf clean all

# Expose Operator Port (HTTP:1080, HTTPS:1443)
EXPOSE 1080 1443
# Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
EXPOSE 2080 2443
# Expose WebRTC
EXPOSE 8443/tcp 15550-15560
# Expose ADB
EXPOSE 6520-6620


# RHEL Orchestration
FROM base AS orchestration
ENTRYPOINT [ "/bin/bash" ]

# Runtime Dependencies
RUN dnf -y install nginx systemd-journal-remote && dnf clean all

# Expose Operator Port (HTTP:1080, HTTPS:1443)
EXPOSE 1080 1443
# Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
EXPOSE 2080 2443
# Expose WebRTC
EXPOSE 8443/tcp 15550-15560
# Expose ADB
EXPOSE 6520-6620
