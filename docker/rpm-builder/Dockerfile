# Dockerfile based on Rocky Linux
# https://hub.docker.com/_/rockylinux
ARG ROCKY_LINUX_VERSION=9.5
FROM rockylinux/rockylinux:$ROCKY_LINUX_VERSION AS builder
ARG BAZEL_VERSION=7.6.0
EXPOSE 80/tcp 8443/tcp
VOLUME [ "/root" ]

LABEL description="RPM Builder" version="1.0.0" repository="https://github.com/google/android-cuttlefish" maintainer="Martin Zeitler" \
org.opencontainers.image.source="https://github.com/google/android-cuttlefish/blob/main/docker/rpm-builder/Dockerfile" \
org.opencontainers.image.title="RPM Builder" \
org.opencontainers.image.description="RPM package builder image based on Rocky Linux" \
org.opencontainers.image.authors="Martin Zeitler" \
org.opencontainers.image.vendor="Google" \
org.opencontainers.image.version="latest" \
org.opencontainers.image.licenses="MIT"

# Install build and runtime dependencies.
# RUN dnf -y swap curl-minimal curl-full
# RUN dnf -y swap coreutils-single coreutils
# RUN dnf -y install dpkg-dev devscripts vim-enhanced
# RUN dnf -y install rpm-devel rpmdevtools rpmlint gcc make python diffutils patch
RUN dnf -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm

RUN dnf -y upgrade
RUN dnf -y install sudo git dnf-plugins-core rpm-build golang-bin qemu-kvm nginx curl-devel systemd-journal-remote
RUN dnf -y install protobuf-devel protobuf-compiler --enablerepo crb
RUN dnf -y install ncurses-compat-libs --enablerepo epel

RUN dnf list installed && dnf clean all

# Add entrypoint scripts.
ADD ./docker/rpm-builder/buildscript.sh /root/buildscript.sh
ADD ./docker/rpm-builder/testscript.sh /root/testscript.sh
RUN chmod +x /root/*.sh
# ENTRYPOINT [ "/root/buildscript.sh" ]
# ENTRYPOINT [ "/root/testscript.sh" ]

# Set up user profile and install `bazelisk`.
RUN echo "USE_BAZEL_VERSION=${BAZEL_VERSION}" >> ~/.bazeliskrc && \
echo "export GOPATH=${HOME}/go" >> ~/.bash_profile && echo "export PATH=${PATH}:${GOPATH}/bin" >> ~/.bash_profile && \
source ~/.bash_profile && go install github.com/bazelbuild/bazelisk@latest
# RUN $GOPATH/bin/bazelisk version
