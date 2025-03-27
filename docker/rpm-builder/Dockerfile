# Dockerfile based on Rocky Linux
# https://hub.docker.com/_/rockylinux
ARG ROCKY_LINUX_VERSION=9.5
FROM rockylinux/rockylinux:$ROCKY_LINUX_VERSION AS builder
ARG BAZEL_VERSION=7.6.0
VOLUME [ "/root" ]

# Expose Operator Port (HTTP:1080, HTTPS:1443)
EXPOSE 1080 1443
# Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
EXPOSE 2080 2443
# Expose WebRTC
EXPOSE 8443/tcp 15550-15560
# Expose ADB
EXPOSE 6520-6620

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

# Install the EPEL repository.
RUN dnf -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm && dnf -y upgrade

# Provide build dependencies.
# RUN dnf -y install dnf-plugins-core
RUN dnf -y install git sudo rpm-build golang-bin ncurses-compat-libs curl-devel
RUN dnf -y install protobuf-devel protobuf-compiler --enablerepo crb

# Provide integration and orchestration dependencies.
RUN dnf -y install qemu-kvm nginx systemd-journal-remote

# RUN dnf list installed
RUN dnf clean all

# Add entrypoint scripts.
ADD ./docker/rpm-builder/buildscript.sh /root/buildscript.sh
ADD ./docker/rpm-builder/testscript.sh /root/testscript.sh
RUN chmod +x /root/*.sh

# ENTRYPOINT [ "/root/buildscript.sh", "/root/testscript.sh" ]
# ENTRYPOINT [ "/root/buildscript.sh" ]
# ENTRYPOINT [ "/root/testscript.sh" ]

# Modify file `~/.bash_profile` and install `bazelisk`.
RUN echo "USE_BAZEL_VERSION=${BAZEL_VERSION}" >> ~/.bazeliskrc && \
echo "export GOPATH=${HOME}/go" >> ~/.bash_profile && echo "export PATH=${PATH}:${GOPATH}/bin" >> ~/.bash_profile && \
source ~/.bash_profile && go install github.com/bazelbuild/bazelisk@latest
# RUN $GOPATH/bin/bazelisk version
