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
ARG USER_UID=1000
ARG USER_GID=1000
ARG USER_NAME=runner

RUN groupadd -g $USER_GID $USER_NAME
RUN useradd -rm -d /home/$USER_NAME -s /bin/bash -g $USER_GID -u $USER_UID $USER_NAME
USER $USER_NAME

# https://docs.docker.com/reference/dockerfile/#notes-about-specifying-volumes
# The host directory is declared at container run-time, with `--volume`, eg.
# --volume /home/username/android-cuttlefish/.rpms/:/home/runner/.rpms
RUN [ "mkdir", "/home/runner/.rpms" ]
VOLUME [ "/home/runner/.rpms" ]

# The EPEL repository installs `dnf-core-plugin` (`config-manager` and `builddeps`) and provides `ncurses-compat-libs`.
# Configuring further package repositories: / https://wiki.rockylinux.org/rocky/repo/#extra-repositories
RUN [ "dnf", "-y", "install", "https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm" ]
RUN [ "rm", "/etc/yum.repos.d/epel-cisco-openh264.repo" ]
RUN [ "dnf", "config-manager", "--set-enabled", "crb" ]
RUN [ "dnf", "-y", "upgrade" ]
RUN [ "dnf", "clean", "all" ]


# Integration
FROM base_arm AS integration
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
# `./tools/rpmbuild/RPMS/x86_64` is only available on Linux.
# However, the `builder` stage will move them to bind-mount.
COPY [ "./.rpms/cuttlefish-base-*", "/root/.rpms/" ]
COPY [ "./.rpms/cuttlefish-integration-*", "/root/.rpms/" ]
COPY [ "./.rpms/cuttlefish-user-*", "/root/.rpms/" ]

# Install Android Cuttlefish.
RUN [ "/bin/bash", "-c", "/root/rpm_install.sh" ]
RUN [ "dnf", "clean", "all" ]
