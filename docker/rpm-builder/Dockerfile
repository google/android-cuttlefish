# Dockerfile based on Rocky Linux
# https://docs.docker.com/reference/dockerfile/
# https://hub.docker.com/_/rockylinux
ARG ROCKY_LINUX_VERSION=9.5
FROM rockylinux/rockylinux:$ROCKY_LINUX_VERSION AS rockylinux
LABEL description="RPM Builder" version="1.0.0" repository="https://github.com/google/android-cuttlefish" maintainer="Martin Zeitler"

# Base Image
FROM rockylinux AS base
WORKDIR "/root"
VOLUME "/root"

# The EPEL repository installs `dnf-core-plugin` (`config-manager` and `builddeps`) and provides `ncurses-compat-libs`.
RUN dnf -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm sudo nano && dnf -y upgrade

# Configuring further package repositories: / https://wiki.rockylinux.org/rocky/repo/#extra-repositories
RUN dnf config-manager --set-enabled crb && dnf config-manager --set-disabled extras && dnf clean all


# RPM Builder
FROM base AS builder
ARG BAZEL_VERSION=7.6.0

# Add entrypoint script.
COPY ./docker/rpm-builder/buildscript.sh /root/entrypoint.sh
ENTRYPOINT [ "/bin/bash", "-c", "./entrypoint.sh" ]
RUN [ "chmod", "+x", "./entrypoint.sh" ]

# Build dependencies.
# RUN dnf -y install dpkg-dev devscripts vim-enhanced
# RUN dnf -y install rpm-devel rpmdevtools rpmlint gcc make python diffutils patch
RUN dnf -y install git wget rpm-build golang-bin curl-devel wayland-devel ncurses-compat-libs \
# Package `protobuf` comes from CRB repository
protobuf protobuf-devel protobuf-compiler && dnf clean all

# Modify file `~/.bash_profile`.
RUN echo "export GOPATH=${HOME}/go" >> ~/.bash_profile \
echo "export PATH=${PATH}:${HOME}/go/bin" >> ~/.bash_profile \
echo "USE_BAZEL_VERSION=${BAZEL_VERSION}" >> ~/.bazeliskrc \
source ~/.bash_profile

# Run go install `bazelisk`.
# RUN go install github.com/bazelbuild/bazelisk@latest
# RUN wget -O /usr/bin/bazel https://github.com/bazelbuild/bazelisk/releases/download/v1.25.0/bazelisk-linux-amd64 && chmod +x /usr/bin/bazel
RUN [ "wget", "-q", "-O", "/usr/bin/bazel", "https://github.com/bazelbuild/bazelisk/releases/download/v1.25.0/bazelisk-linux-amd64" ]

# RPM Testing
FROM base AS testing

# Add entrypoint script.
COPY ./docker/rpm-builder/testscript.sh /root/entrypoint.sh
ENTRYPOINT [ "/bin/bash", "-c", "./entrypoint.sh" ]
RUN [ "chmod", "+x", "./entrypoint.sh" ]

# Runtime Dependencies
RUN dnf -y install libwayland-* && dnf clean all

# Integration Test
# FROM base AS integration
# ENTRYPOINT [ "/bin/sh" ]

# Expose Operator Port (HTTP:1080, HTTPS:1443)
# EXPOSE 1080 1443
# Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
# EXPOSE 2080 2443
# Expose WebRTC
# EXPOSE 8443/tcp 15550-15560
# Expose ADB
# EXPOSE 6520-6620

# Runtime Dependencies
# RUN dnf -y install qemu-kvm && dnf clean all


# Orchestration Test
# FROM base AS orchestration
# ENTRYPOINT [ "/bin/sh" ]

# Expose Operator Port (HTTP:1080, HTTPS:1443)
# EXPOSE 1080 1443
# Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
# EXPOSE 2080 2443
# Expose WebRTC
# EXPOSE 8443/tcp 15550-15560
# Expose ADB
# EXPOSE 6520-6620

# Runtime Dependencies
# RUN dnf -y install nginx systemd-journal-remote && dnf clean all
