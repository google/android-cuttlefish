# Dockerfile based on Rocky Linux
# https://hub.docker.com/_/rockylinux
FROM rockylinux/rockylinux:9.5 AS builder
ARG BAZEL_VERSION=7.6.0
ARG USERNAME="runner"

LABEL description="RPM Builder" version="1.0.0" repository="https://github.com/google/android-cuttlefish" maintainer="Martin Zeitler" \
org.opencontainers.image.source="https://github.com/google/android-cuttlefish/blob/main/docker/rpm-builder/Dockerfile" \
org.opencontainers.image.title="RPM Builder" \
org.opencontainers.image.description="RPM package builder image based on Rocky Linux" \
org.opencontainers.image.authors="Martin Zeitler" \
org.opencontainers.image.vendor="Google" \
org.opencontainers.image.version="latest" \
org.opencontainers.image.licenses="MIT"

# Volume, ports and user.
EXPOSE 80/tcp 8443/tcp
VOLUME [ "/home/$USERNAME" ]
RUN adduser $USERNAME
USER $USERNAME

# Add entrypoint scripts.
ADD ./docker/rpm-builder/buildscript.sh /home/$USERNAME/buildscript.sh
ADD ./docker/rpm-builder/testscript.sh /home/$USERNAME/testscript.sh

# Build and runtime dependencies.
# RUN dnf -y swap curl-minimal curl-full
# RUN dnf -y swap coreutils-single coreutils
# RUN dnf -y install dpkg-dev devscripts vim-enhanced
# RUN dnf -y install rpm-devel rpmdevtools rpmlint gcc make python diffutils patch
RUN dnf -y upgrade && dnf -y install dnf-plugins-core sudo rpm-build git nginx golang-bin curl-devel qemu-kvm systemd-journal-remote
RUN dnf -y install protobuf-devel protobuf-compiler --enablerepo crb && dnf list installed && dnf clean all



# Install `bazelisk`.
RUN echo "USE_BAZEL_VERSION=${BAZEL_VERSION}" >> ~/.bazeliskrc && \
echo "export GOPATH=${HOME}/go" >> ~/.bash_profile && echo "export PATH=${PATH}:${GOPATH}/bin" >> ~/.bash_profile && \
source ~/.bash_profile && go install github.com/bazelbuild/bazelisk@latest
# RUN $GOPATH/bin/bazelisk version

# Add entrypoint scripts.
USER root
ADD ./docker/rpm-builder/buildscript.sh /home/$USERNAME/buildscript.sh
ADD ./docker/rpm-builder/testscript.sh /home/$USERNAME/testscript.sh
RUN chown $USERNAME:$USERNAME /home/$USERNAME/*.sh && chmod +x /home/$USERNAME/*.sh
