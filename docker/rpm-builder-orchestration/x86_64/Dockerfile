# Dockerfile based on `rockylinux/rockylinux:9.5`
FROM rockylinux/rockylinux:9.5 AS linux
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
FROM linux AS base_x86
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


# Host Orchestration
FROM base_x86 AS orchestration
ENTRYPOINT [ "/root/services.sh" ]

# Expose Operator Port (HTTP:1080, HTTPS:1443)
EXPOSE 1080 1443
# Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
EXPOSE 2080 2443
# Expose WebRTC
EXPOSE 15550-15560
# Expose ADB
EXPOSE 6520-6620

# Add prebuilt RPM packages.
COPY [ "./.rpms/cuttlefish-base-*", "/root/.rpms/" ]
COPY [ "./.rpms/cuttlefish-orchestration-*", "/root/.rpms/" ]
RUN [ "/bin/bash", "-c", "/root/rpm_install.sh" ]

COPY [ "./docker/rpm-builder/scripts/services.sh", "/root/services.sh" ]
RUN [ "chmod", "+x", "/root/services.sh" ]
