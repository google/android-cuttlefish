# Dockerfile based on Rocky Linux
# https://hub.docker.com/_/rockylinux
ARG ROCKY_LINUX_VERSION=9.5
FROM rockylinux/rockylinux:$ROCKY_LINUX_VERSION AS builder
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

# Install build and runtime dependencies.
# RUN dnf -y swap curl-minimal curl-full
# RUN dnf -y swap coreutils-single coreutils
# RUN dnf -y install dpkg-dev devscripts vim-enhanced
# RUN dnf -y install rpm-devel rpmdevtools rpmlint gcc make python diffutils patch
RUN dnf -y upgrade
RUN dnf -y install sudo git dnf-plugins-core rpm-build golang-bin qemu-kvm nginx curl-devel systemd-journal-remote
RUN dnf -y install protobuf-devel protobuf-compiler --enablerepo crb
RUN dnf list installed && dnf clean all

# Volume, ports and user.
RUN adduser $USERNAME
USER $USERNAME
VOLUME [ "/home/${USERNAME}" ]

# Add entrypoint scripts.
ADD ./docker/rpm-builder/buildscript.sh /home/$USERNAME/buildscript.sh
ADD ./docker/rpm-builder/testscript.sh /home/$USERNAME/testscript.sh
EXPOSE 80/tcp 8443/tcp
# ENTRYPOINT [ "/home/$USERNAME/testscript.sh" ]
ENTRYPOINT [ "/home/$USERNAME/buildscript.sh" ]
CMD [ "--rpm-build" ]

# Set up user profile and install `bazelisk`.
RUN echo "USE_BAZEL_VERSION=${BAZEL_VERSION}" >> ~/.bazeliskrc && \
echo "export GOPATH=${HOME}/go" >> ~/.bash_profile && echo "export PATH=${PATH}:${GOPATH}/bin" >> ~/.bash_profile && \
source ~/.bash_profile && go install github.com/bazelbuild/bazelisk@latest
# RUN $GOPATH/bin/bazelisk version \

USER root
RUN chown $USERNAME:$USERNAME /home/$USERNAME/*.sh && chmod +x /home/$USERNAME/*.sh
