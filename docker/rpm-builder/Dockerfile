# Dockerfile based on Rocky Linux
FROM rockylinux/rockylinux:9.5 AS linux
LABEL description="RPM Builder" version="1.0.0" repository="https://github.com/google/android-cuttlefish" maintainer="Martin Zeitler"

# Argumentss
# TODO: To be changed to `google` removed upon merging to `main` branch; also see clone.sh.
ARG USER="syslogic"
ARG REPO="android-cuttlefish"

# TODO: PGP key passphrase to be provided with GitHub secrets.
ARG PASSPHRASE=""



# Base Image
FROM linux AS base_x86
ENV PGP_PASSPHRASE="${PASSPHRASE}"
ENV REPO_USER="${USER}"
ENV REPO_NAME="${REPO}"

# https://docs.docker.com/reference/dockerfile/#notes-about-specifying-volumes
# The host directory is declared at container run-time, with --volume.
# /home/username/android-cuttlefish/.cache:/root/.cache
# /home/username/android-cuttlefish/.rpms/:/root/.rpms
RUN [ "mkdir", "/root/.cache" ]
RUN [ "mkdir", "/root/.rpms" ]
VOLUME [ "/root/.cache" ]
WORKDIR "/root"

# The EPEL repository installs `dnf-core-plugin` (`config-manager` and `builddeps`) and provides `ncurses-compat-libs`.
# Configuring further package repositories: / https://wiki.rockylinux.org/rocky/repo/#extra-repositories
RUN [ "dnf", "-y", "install", "https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm" ]
RUN [ "rm", "/etc/yum.repos.d/rocky-extras.repo", "/etc/yum.repos.d/epel-cisco-openh264.repo", "/root/.cshrc", "/root/.tcshrc" ]
RUN [ "dnf", "config-manager", "--set-enabled", "crb" ]
RUN [ "dnf", "-y", "upgrade" ]
RUN [ "dnf", "clean", "all" ]

# RPM Builder
FROM base_x86 AS builder
ARG BAZEL_VERSION=7.6.0

# Add container scripts.
COPY [ "./docker/rpm-builder/scripts/build.sh", "/root/build.sh" ]
COPY [ "./docker/rpm-builder/scripts/clone.sh", "/root/clone.sh" ]
COPY [ "./docker/rpm-builder/scripts/install.sh", "/root/install.sh" ]
# COPY [ "./docker/rpm-builder/scripts/sign.sh", "/root/sign.sh" ]
# COPY [ "./docker/rpm-builder/scripts/sign.exp", "/usr/bin/sign.exp" ]
# RUN [ "chmod", "+x", "/root/clone.sh", "/root/build.sh", "/root/install.sh", "/usr/bin/sign.exp", "/root/sign.sh" ]
RUN [ "chmod", "+x", "/root/clone.sh", "/root/build.sh", "/root/install.sh" ]
ENTRYPOINT [ "/bin/bash", "-c", "./build.sh", "${OWNER}", "${REPO_NAME}" ]
RUN [ "touch", "/root/.dockerfile" ]

# Build dependencies.
# RUN dnf -y install dpkg-dev devscripts
# RUN dnf -y install rpm-devel rpmdevtools rpmlint gcc make python diffutils patch
RUN [ "dnf", "config-manager", "--set-enabled", "crb" ]
RUN [ "dnf", "-y", "install", "sudo", "nano", "git", "cmake", "gcc-c++", "rpm-build", "golang-bin", "expect", "vim-common", "ncurses-compat-libs", "curl-devel", "wayland-devel", "protobuf-devel", "protobuf-compiler", "ca-certificates" ]
RUN [ "dnf", "-y", "upgrade" ]
RUN [ "dnf", "clean", "all" ]
RUN [ "dnf", "clean", "all" ]

# Modify files `~/.bazeliskrc` and `~/.bash_profile`.
RUN [ "echo", "USE_BAZEL_VERSION=${BAZEL_VERSION}", ">>", "~/.bazeliskrc", "&&", "echo", "export", "GOPATH=${HOME}/go", ">>", "~/.bash_profile", "&&", "echo", "export", "PATH=${PATH}:${HOME}/go/bin", ">>", "~/.bash_profile" ]
RUN source ~/.bash_profile

# Go install `bazelisk`.
RUN [ "go", "install", "github.com/bazelbuild/bazelisk@latest" ]


# RPM Testing
FROM builder AS testing
ENTRYPOINT [ "/bin/bash" ]


# Integration
FROM linux AS integration
ENTRYPOINT [ "/bin/bash" ]

# Expose Operator Port (HTTP:1080, HTTPS:1443)
EXPOSE 1080 1443
# Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
EXPOSE 2080 2443
# Expose WebRTC
EXPOSE 15550-15560
# Expose ADB
EXPOSE 6520-6620

# Add prebuilt RPM packages.
COPY [ "./tools/rpmbuild/RPMS/x86_64/cuttlefish-base-*", "/root/.rpms/" ]
COPY [ "./tools/rpmbuild/RPMS/x86_64/cuttlefish-integration-*", "/root/.rpms/" ]
COPY [ "./tools/rpmbuild/RPMS/x86_64/cuttlefish-orchestration-*", "/root/.rpms/" ]
COPY [ "./tools/rpmbuild/RPMS/x86_64/cuttlefish-user-*", "/root/.rpms/" ]

# Install Android Cuttlefish.
RUN [ "dnf", "-y", "install", "/root/.rpms/cuttlefish-" ]
RUN [ "dnf", "clean", "all" ]
RUN [ "cvd", "--version" ]


# Host Orchestration
FROM base AS orchestration
ENTRYPOINT [ "/root/services.sh" ]

COPY [ "./docker/rpm-builder/scripts/services.sh", "/root/services.sh" ]
RUN [ "chmod", "+x", "/root/services.sh" ]

# RUN [ "/bin/bash", "-c", "./install.sh", "${OWNER}", "${REPO_NAME}" ]

# Expose Operator Port (HTTP:1080, HTTPS:1443)
EXPOSE 1080 1443
# Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
EXPOSE 2080 2443
# Expose WebRTC
EXPOSE 15550-15560
# Expose ADB
EXPOSE 6520-6620

# Runtime Dependencies
RUN [ "dnf", "-y", "install", "nginx", "systemd", "systemd-udev",  "systemd-journal-remote" ]
RUN [ "dnf", "clean", "all" ]

RUN [ "echo", "num_cvd_accounts=100", ">>", "/etc/default/cuttlefish-host-resources" ]
RUN [ "groupadd", "--system", "cvdnetwork" ]

RUN [ "systemctl", "start", "udevd" ]
RUN [ "udevadm", "control", "--reload-rules" ]
RUN [ "udevadm", "trigger" ]
RUN [ "systemctl", "restart", "NetworkManager" ]
