# Dockerfile for building RPM packages.
# https://hub.docker.com/_/rockylinux
# source ~/.bash_profile
FROM rockylinux/rockylinux:9.5 AS builder
ARG BAZEL_VERSION=7.6.0
EXPOSE 80/tcp 8443/tcp
VOLUME "/home/runner"

LABEL description="RPM Builder" version="1.0.0" repository="https://github.com/google/android-cuttlefish" maintainer="Martin Zeitler" \
org.opencontainers.image.source="https://github.com/google/android-cuttlefish/blob/main/docker/rpm-builder/Dockerfile" \
org.opencontainers.image.title="RPM Builder" \
org.opencontainers.image.description="RPM builder image based on Rocky Linux" \
org.opencontainers.image.authors="Martin Zeitler" \
org.opencontainers.image.vendor="Google" \
org.opencontainers.image.version="latest" \
org.opencontainers.image.licenses="MIT"

# Build and runtime dependencies.
# RUN dnf -y swap curl-minimal curl-full
# RUN dnf -y swap coreutils-single coreutils
# RUN dnf -y install dpkg-dev devscripts vim-enhanced
# RUN dnf -y install rpm-devel rpmdevtools rpmlint gcc make python diffutils patch
RUN dnf -y upgrade && dnf -y install sudo rpm-build git nginx golang-bin curl-devel qemu-kvm \
systemd-journal-remote protobuf-devel protobuf-compiler --enablerepo crb && \
dnf list installed && dnf clean all

# Install `bazelisk`.
RUN echo "USE_BAZEL_VERSION=${BAZEL_VERSION}" >> ~/.bazeliskrc && \
echo "export GOPATH=${HOME}/go" >> ~/.bashrc && echo "export PATH=${PATH}:${GOPATH}/bin" >> ~/.bashrc && \
source ~/.bashrc && go install github.com/bazelbuild/bazelisk@latest
# RUN $GOPATH/bin/bazelisk version

RUN adduser runner

# Entrypoint Script.
COPY ./docker/rpm-builder/buildscript.sh /home/runner/buildscript.sh
# ENTRYPOINT [ "/buildscript.sh" ]

