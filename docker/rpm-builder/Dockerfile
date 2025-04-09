# Dockerfile based on `rockylinux/rockylinux:9.5`
ARG LINUX_IMAGE=rockylinux/rockylinux:9.5
FROM $LINUX_IMAGE AS linux

LABEL description="RPM Builder" version="1.0.1" repository="https://github.com/google/android-cuttlefish" maintainer="Martin Zeitler"
WORKDIR "/root"

# Arguments
# TODO: To be changed to `google` upon merging to `main` branch.
# This is merely being used in `clone.sh`, to switch the correct brach.
ARG GITHUB_USER="syslogic"
ARG GUTHUB_REPO="android-cuttlefish"
ARG GITHUB_BRANCH="rpmbuild"

# PGP Arguments
# TODO: PGP signature and passphrase are required to sign packages.
ARG PGP_SIGNATURE=""
ARG PGP_PASSPHRASE=""

# Don't ask for the `sudo` password.
RUN [ "echo", "%docker", "ALL=(ALL)", "NOPASSWD:", "ALL", ">", "/etc/sudoers.d/docker.conf" ]

# Adding the user provides the home directory.
RUN [ "groupadd", "-g", "117", "docker" ]
RUN [ "useradd", "-g", "117", "-u", "1001", "-s", "/bin/bash", "runner" ]

# Note: The container scripts are checking for this file,
# in order to prevent accidentlly running them on the host.
RUN [ "touch", "/home/runner/.dockerenv" ]

# Base Image
FROM linux AS base_image

# The EPEL repository installs `dnf-core-plugin` (`config-manager` and `builddeps`) and provides `ncurses-compat-libs`.
# Configuring further package repositories: / https://wiki.rockylinux.org/rocky/repo/#extra-repositories
RUN [ "dnf", "-y", "install", "https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm" ]
RUN [ "rm", "/etc/yum.repos.d/epel-cisco-openh264.repo" ]
RUN [ "dnf", "config-manager", "--set-enabled", "crb" ]
RUN [ "dnf", "-y", "upgrade" ]
RUN [ "dnf", "clean", "all" ]

# https://docs.docker.com/reference/dockerfile/#notes-about-specifying-volumes
# The host directory is declared at container run-time, with `--volume`, eg.
# --volume /home/username/android-cuttlefish/.cache:/home/runner/.cache \
# --volume /home/username/android-cuttlefish/.rpms/:/home/runner/.rpms
RUN [ "mkdir", "-p", "/home/runner/.cache", "/home/runner/.rpms" ]
VOLUME [ "/home/runner/.cache", "/home/runner/.rpms" ]

# RPM Builder
FROM base_image AS builder
ENV REPO_USER="${GITHUB_USER}"
ENV REPO_NAME="${GUTHUB_REPO}"
ENV REPO_BRANCH="${GITHUB_BRANCH}"

ENV RPM_SIGN_SIGNATURE="${PGP_SIGNATURE}"
ENV RPM_SIGN_PASSPHRASE="${PGP_PASSPHRASE}"

ENTRYPOINT [ "/bin/bash", "-c", "/home/runner/build.sh", "${REPO_USER}", "${REPO_NAME}", "${REPO_BRANCH}" ]

# Build dependencies.
RUN [ "dnf", "-y", "install", "sudo", "nano", "git", "wget", "cmake", "gcc-c++", "rpm-build", "golang-bin", "expect", "vim-common", "ncurses-compat-libs", "curl-devel", "wayland-devel", "protobuf-devel", "protobuf-compiler", "ca-certificates" ]
RUN [ "dnf", "-y", "upgrade" ]
RUN [ "dnf", "clean", "all" ]

# Install `bazelisk` for all users.
RUN if [ "$(uname -i)" =  "x86_64" ]; then wget --no-verbose --output-document /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/download/v1.25.0/bazelisk-linux-amd64; fi
RUN if [ "$(uname -i)" = "aarch64" ]; then wget --no-verbose --output-document /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/download/v1.25.0/bazelisk-linux-arm64; fi
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
