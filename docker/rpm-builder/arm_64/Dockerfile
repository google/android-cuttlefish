# Dockerfile based on `rockylinux/rockylinux:9.5` or `arm64v8/rockylinux:9.3`
ARG LINUX_IMAGE=arm64v8/rockylinux:9.3
FROM $LINUX_IMAGE AS linux

LABEL description="RPM Builder" version="1.0.0" repository="https://github.com/google/android-cuttlefish" maintainer="Martin Zeitler"
WORKDIR "/root"

# GitHub Arguments
# TODO: To be changed to `google` upon merging to `main` branch.
# This is merely being used in `clone.sh`, to switch the correct brach.
ARG USER="syslogic"
ARG REPO="android-cuttlefish"

# PGP Arguments
# TODO: PGP signature and passphrase are required to sign packages.
ARG SIGNATURE=""
ARG PASSPHRASE=""

# Decluttering some unused files.
RUN [ "rm", "/root/.cshrc", "/root/.tcshrc" ]

# Note: The container scripts are checking for this file,
# in order to prevent accidentlly running them on the host.
RUN [ "touch", "/root/.dockerenv" ]

# Adding the user provides the home directory.
RUN [ "groupadd", "-g", "117", "docker" ]
RUN [ "useradd", "-g", "117", "-u", "1001", "-s", "/bin/bash", "runner" ]
RUN [ "mkdir", "-p", "/github/home" ]
RUN [ "chown", "-R", "runner:docker", "/github" ]
RUN [ "ln", "-s", "/github/home", "/home/runner" ]


# Base Image
FROM linux AS base
ENV PGP_SIGNATURE="${SIGNATURE}"
ENV PGP_PASSPHRASE="${PASSPHRASE}"
ENV REPO_USER="${USER}"
ENV REPO_NAME="${REPO}"

# https://docs.docker.com/reference/dockerfile/#notes-about-specifying-volumes
# The host directory is declared at container run-time, with `--volume`, eg.
# --volume /home/username/android-cuttlefish/.cache:/home/runner/.cache \
# --volume /home/username/android-cuttlefish/.rpms/:/home/runner/.rpms
WORKDIR "/home/runner"
RUN [ "mkdir", "-p", "/home/runner/.cache", "/home/runner/.rpms" ]
VOLUME [ "/home/runner/.cache", "/home/runner/.rpms" ]

# The EPEL repository installs `dnf-core-plugin` (`config-manager` and `builddeps`) and provides `ncurses-compat-libs`.
# Configuring further package repositories: / https://wiki.rockylinux.org/rocky/repo/#extra-repositories
RUN [ "dnf", "-y", "install", "https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm" ]
RUN [ "rm", "/etc/yum.repos.d/epel-cisco-openh264.repo" ]
RUN [ "dnf", "config-manager", "--set-enabled", "crb" ]
RUN [ "dnf", "-y", "upgrade" ]
RUN [ "dnf", "clean", "all" ]


# RPM Builder
FROM base AS builder
ENTRYPOINT [ "/bin/bash", "-c", "/home/runner/build.sh", "${OWNER}", "${REPO_NAME}" ]

# Build dependencies.
RUN [ "dnf", "-y", "install", "sudo", "nano", "git", "wget", "cmake", "gcc-c++", "rpm-build", "golang-bin", "expect", "vim-common", "ncurses-compat-libs", "curl-devel", "wayland-devel", "protobuf-devel", "protobuf-compiler", "ca-certificates" ]
RUN [ "dnf", "-y", "upgrade" ]
RUN [ "dnf", "clean", "all" ]

# Install `bazelisk` for all users.
RUN [ "wget", "--no-verbose", "--output-document", "/usr/local/bin/bazel", "https://github.com/bazelbuild/bazelisk/releases/download/v1.25.0/bazelisk-linux-arm64" ]
RUN [ "chmod", "+x", "/usr/local/bin/bazel" ]

# Container scripts.
RUN [ "touch", "/home/runner/.dockerenv" ]
RUN [ "cp", "/root/.bash_profile", "/home/runner/.bash_profile" ]
COPY [ "./docker/rpm-builder/scripts/build.sh", "/home/runner/build.sh" ]
COPY [ "./docker/rpm-builder/scripts/clone.sh", "/home/runner/clone.sh" ]
COPY [ "./docker/rpm-builder/scripts/sign_rpm.sh", "/home/runner/sign_rpm.sh" ]
COPY [ "./docker/rpm-builder/scripts/sign_rpm.exp", "/home/runner/sign_rpm.exp" ]
COPY [ "./docker/rpm-builder/scripts/install_rpm.sh", "/home/runner/install_rpm.sh" ]
COPY [ "./docker/rpm-builder/scripts/install_src.sh", "/home/runner/install_src.sh" ]
RUN [ "echo", "%docker", "ALL=(ALL)", "NOPASSWD:", "ALL", ">", "/etc/sudoers.d/docker.conf" ]
RUN [ "echo", "export", "GOPATH=/home/runner/go", ">>", "/home/runner/.bash_profile" ]
RUN [ "echo", "export", "PATH=$PATH:/home/runner/go/bin", ">>", "/home/runner/.bash_profile" ]
RUN [ "chmod", "+x", "/home/runner/install_rpm.sh", "/home/runner/install_src.sh" ]
RUN [ "chmod", "+x", "/home/runner/sign_rpm.sh", "/home/runner/sign_rpm.exp" ]
RUN [ "chmod", "+x", "/home/runner/build.sh", "/home/runner/clone.sh" ]
RUN [ "chown", "-R", "runner:docker", "/home/runner" ]

# Run commands as by default as user $UID,
# else the bind-mounts will be owned by root.
USER "runner"
RUN source ~/.bash_profile


# RPM Testing
FROM builder AS testing
ENTRYPOINT [ "/bin/bash" ]
