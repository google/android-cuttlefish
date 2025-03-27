# Dockerfile based on Rocky Linux
# https://hub.docker.com/_/rockylinux
ARG ROCKY_LINUX_VERSION=9.5
ARG BAZEL_VERSION=7.6.0
ARG USERNAME="runner"

FROM rockylinux/rockylinux:$ROCKY_LINUX_VERSION AS builder
LABEL description="RPM Builder" version="1.0.0" repository="https://github.com/google/android-cuttlefish" maintainer="Martin Zeitler" \
org.opencontainers.image.source="https://github.com/google/android-cuttlefish/blob/main/docker/rpm-builder/Dockerfile" \
org.opencontainers.image.title="RPM Builder" \
org.opencontainers.image.description="RPM package builder image based on Rocky Linux" \
org.opencontainers.image.authors="Martin Zeitler" \
org.opencontainers.image.vendor="Google" \
org.opencontainers.image.version="latest" \
org.opencontainers.image.licenses="MIT"

# Volume, ports and user.
USER $USERNAME
RUN adduser $USERNAME
VOLUME [ "/home/$USERNAME" ]
EXPOSE 80/tcp 8443/tcp

# Add entrypoint scripts.
ADD ./docker/rpm-builder/buildscript.sh /home/$USERNAME/buildscript.sh
ADD ./docker/rpm-builder/testscript.sh /home/$USERNAME/testscript.sh
# ENTRYPOINT [ "/bin/bash", "-c", "/home/$USERNAME/testscript.sh" ]
ENTRYPOINT [ "/bin/bash", "-c", "/home/$USERNAME/buildscript.sh" ]
CMD [ "--rpm-build" ]

# Install build and runtime dependencies.
# RUN dnf -y swap curl-minimal curl-full
# RUN dnf -y swap coreutils-single coreutils
# RUN dnf -y install dpkg-dev devscripts vim-enhanced
# RUN dnf -y install rpm-devel rpmdevtools rpmlint gcc make python diffutils patch
RUN dnf -y upgrade && \
dnf -y install dnf-plugins-core sudo rpm-build git nginx golang-bin curl-devel qemu-kvm systemd-journal-remote && \
dnf -y install protobuf-devel protobuf-compiler --enablerepo crb && \
dnf list installed && dnf clean all

# Set up user profile and install `bazelisk`.
RUN echo "USE_BAZEL_VERSION=${BAZEL_VERSION}" >> ~/.bazeliskrc && \
echo "export GOPATH=${HOME}/go" >> ~/.bash_profile && echo "export PATH=${PATH}:${GOPATH}/bin" >> ~/.bash_profile && \
source ~/.bash_profile && chown $USERNAME:$USERNAME /home/$USERNAME/*.sh && chmod +x /home/$USERNAME/*.sh && \
go install github.com/bazelbuild/bazelisk@latest
# RUN $GOPATH/bin/bazelisk version
