# Dockerfile based on `arm64v8/rockylinux:9.3`
FROM --platform=linux/arm64/v8 arm64v8/rockylinux:9.3 AS linux
LABEL description="RPM Builder" version="1.0.0" repository="https://github.com/google/android-cuttlefish" maintainer="Martin Zeitler"
WORKDIR "/root"

# Note: The container scripts are checking for this file,
# in order to prevent accidentlly running them on the host.
RUN [ "touch", "/root/.dockerfile" ]

# Decluttering some unused files.
RUN [ "rm", "/root/.cshrc", "/root/.tcshrc" ]

# GitHub Arguments
# TODO: To be changed to `google` upon merging to `main` branch.
# This is merely being used in `clone.sh`, to switch the correct brach.
ARG USER="syslogic"
ARG REPO="android-cuttlefish"

# PGP Arguments
# TODO: PGP signature and passphrase are required to sign packages.
ARG SIGNATURE=""
ARG PASSPHRASE=""


# Base Image
FROM linux AS base_arm
ENV PGP_SIGNATURE="${SIGNATURE}"
ENV PGP_PASSPHRASE="${PASSPHRASE}"
ENV REPO_USER="${USER}"
ENV REPO_NAME="${REPO}"

# Required for GitHub.
ARG USER_UID=1001
ARG USER_GID=1001
ARG USER_NAME=runner

RUN groupadd -g $USER_GID $USER_NAME
RUN useradd -rm -d /home/$USER_NAME -s /bin/bash -g $USER_GID -u $USER_UID $USER_NAME
RUN [ "touch", "/home/runner/.dockerfile" ]

# https://docs.docker.com/reference/dockerfile/#notes-about-specifying-volumes
# The host directory is declared at container run-time, with `--volume`, eg.
# --volume /home/username/android-cuttlefish/.cache:/home/runner/.cache \
# --volume /home/username/android-cuttlefish/.rpms/:/home/runner/.rpms
RUN [ "mkdir", "/home/runner/.cache", "/home/runner/.rpms" ]
VOLUME [ "/home/runner/.cache", "/home/runner/.rpms" ]

# The EPEL repository installs `dnf-core-plugin` (`config-manager` and `builddeps`) and provides `ncurses-compat-libs`.
# Configuring further package repositories: / https://wiki.rockylinux.org/rocky/repo/#extra-repositories
RUN [ "dnf", "-y", "install", "https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm" ]
RUN [ "rm", "/etc/yum.repos.d/epel-cisco-openh264.repo" ]
RUN [ "dnf", "config-manager", "--set-enabled", "crb" ]
RUN [ "dnf", "-y", "upgrade" ]
RUN [ "dnf", "clean", "all" ]


# RPM Builder
FROM base_arm AS builder
ENTRYPOINT [ "/bin/bash", "-c", "./build.sh", "${OWNER}", "${REPO_NAME}" ]

# Build dependencies.
# RUN dnf -y install dpkg-dev devscripts
# RUN dnf -y install rpm-devel rpmdevtools rpmlint
RUN [ "dnf", "-y", "install", "sudo", "nano", "git", "cmake", "gcc-c++", "rpm-build", "golang-bin", "expect", "vim-common", "ncurses-compat-libs", "curl-devel", "wayland-devel", "protobuf-devel", "protobuf-compiler", "ca-certificates" ]
RUN [ "dnf", "-y", "upgrade" ]
RUN [ "dnf", "clean", "all" ]

# Go install `bazelisk`.
RUN [ "go", "install", "github.com/bazelbuild/bazelisk@latest" ]
RUN [ "mv", "/root/go", "/home/runner/go" ]

# Modify file `~/.bash_profile`.
RUN [ "echo", "export", "GOPATH=/home/runner/go", ">>", "/home/runner/.bash_profile", "&&", "echo", "export", "PATH=${PATH}:/home/runner/go/bin", ">>", "/home/runner/.bash_profile" ]
RUN source /home/runner/.bash_profile

# Add container scripts.
COPY [ "./docker/rpm-builder/scripts/build.sh", "/home/runner/build.sh" ]
COPY [ "./docker/rpm-builder/scripts/clone.sh", "/home/runner/clone.sh" ]
# COPY [ "./docker/rpm-builder/scripts/sign_rpm.sh", "/home/runner/sign_rpm.sh" ]
# COPY [ "./docker/rpm-builder/scripts/sign_rpm.exp", "/usr/bin/sign_rpm.exp" ]
COPY [ "./docker/rpm-builder/scripts/install_rpm.sh", "/home/runner/install_rpm.sh" ]
COPY [ "./docker/rpm-builder/scripts/install_src.sh", "/home/runner/install_src.sh" ]
RUN [ "chmod", "+x", "/home/runner/build.sh", "/home/runner/clone.sh" ]
RUN [ "chown", "runner:runner", "/home/runner/build.sh" ]
RUN [ "chown", "runner:runner", "/home/runner/clone.sh" ]

# Set user.
USER $USER_NAME


# RPM Testing
FROM builder AS testing
ENTRYPOINT [ "/bin/bash" ]