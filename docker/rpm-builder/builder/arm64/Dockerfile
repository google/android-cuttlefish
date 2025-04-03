# Dockerfile based on `arm64v8/rockylinux:9.3`
FROM arm64v8/rockylinux:9.3 AS linux
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

# https://docs.docker.com/reference/dockerfile/#notes-about-specifying-volumes
# The host directory is declared at container run-time, with `--volume`, eg.
# --volume /home/username/android-cuttlefish/.cache:/root/.cache \
# --volume /home/username/android-cuttlefish/.rpms/:/root/.rpms
RUN [ "mkdir", "/root/.cache", "/root/.rpms" ]
VOLUME [ "/root/.cache", "/root/.rpms" ]

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
ARG BAZEL_VERSION=7.6.0

# Add container scripts.
COPY [ "./docker/rpm-builder/scripts/build.sh", "/root/build.sh" ]
COPY [ "./docker/rpm-builder/scripts/clone.sh", "/root/clone.sh" ]
# COPY [ "./docker/rpm-builder/scripts/sign_rpm.sh", "/root/sign_rpm.sh" ]
# COPY [ "./docker/rpm-builder/scripts/sign_rpm.exp", "/usr/bin/sign_rpm.exp" ]
COPY [ "./docker/rpm-builder/scripts/install_rpm.sh", "/root/install_rpm.sh" ]
COPY [ "./docker/rpm-builder/scripts/install_src.sh", "/root/install_src.sh" ]
RUN [ "chmod", "+x", "/root/clone.sh", "/root/build.sh", "/root/install_rpm.sh", "/root/install_src.sh" ]

# Build dependencies.
# RUN dnf -y install dpkg-dev devscripts
# RUN dnf -y install rpm-devel rpmdevtools rpmlint
RUN [ "dnf", "-y", "install", "sudo", "nano", "git", "cmake", "gcc-c++", "rpm-build", "golang-bin", "expect", "vim-common", "ncurses-compat-libs", "curl-devel", "wayland-devel", "protobuf-devel", "protobuf-compiler", "ca-certificates" ]
RUN [ "dnf", "-y", "upgrade" ]
RUN [ "dnf", "clean", "all" ]

# Modify files `~/.bazeliskrc` and `~/.bash_profile`.
RUN [ "echo", "USE_BAZEL_VERSION=${BAZEL_VERSION}", ">>", "~/.bazeliskrc", "&&", "echo", "export", "GOPATH=${HOME}/go", ">>", "~/.bash_profile", "&&", "echo", "export", "PATH=${PATH}:${HOME}/go/bin", ">>", "~/.bash_profile" ]
RUN source ~/.bash_profile

# Go install `bazelisk`.
RUN [ "go", "install", "github.com/bazelbuild/bazelisk@latest" ]


# RPM Testing
FROM builder AS testing
ENTRYPOINT [ "/bin/bash" ]


# # Integration
# FROM base_x86 AS integration
# ENTRYPOINT [ "/bin/bash" ]
#
# # Expose Operator Port (HTTP:1080, HTTPS:1443)
# EXPOSE 1080 1443
# # Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
# EXPOSE 2080 2443
# # Expose WebRTC
# EXPOSE 15550-15560
# # Expose ADB
# EXPOSE 6520-6620
#
# # Add prebuilt RPM packages.
# # `./tools/rpmbuild/RPMS/x86_64` is only available on Linux.
# # However, the `builder` stage will move them to bind-mount.
# COPY [ "./.rpms/cuttlefish-base-*", "/root/.rpms/" ]
# COPY [ "./.rpms/cuttlefish-integration-*", "/root/.rpms/" ]
# COPY [ "./.rpms/cuttlefish-user-*", "/root/.rpms/" ]
#
# # Install Android Cuttlefish.
# RUN [ "/bin/bash", "-c", "/root/rpm_install.sh" ]
# RUN [ "dnf", "clean", "all" ]
#
#
# # Host Orchestration
# FROM base_x86 AS orchestration
# ENTRYPOINT [ "/root/services.sh" ]
#
# # Expose Operator Port (HTTP:1080, HTTPS:1443)
# EXPOSE 1080 1443
# # Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
# EXPOSE 2080 2443
# # Expose WebRTC
# EXPOSE 15550-15560
# # Expose ADB
# EXPOSE 6520-6620
#
# # Add prebuilt RPM packages.
# COPY [ "./.rpms/cuttlefish-base-*", "/root/.rpms/" ]
# COPY [ "./.rpms/cuttlefish-orchestration-*", "/root/.rpms/" ]
# RUN [ "/bin/bash", "-c", "/root/rpm_install.sh" ]
#
# COPY [ "./docker/rpm-builder/scripts/services.sh", "/root/services.sh" ]
# RUN [ "chmod", "+x", "/root/services.sh" ]
