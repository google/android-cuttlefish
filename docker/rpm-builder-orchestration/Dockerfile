# Dockerfile based on `rockylinux/rockylinux:9.5` or `arm64v8/rockylinux:9.3`
ARG LINUX_IMAGE=rockylinux/rockylinux:9.5
FROM $LINUX_IMAGE AS linux

LABEL description="RPM Builder" version="1.0.1" repository="https://github.com/google/android-cuttlefish" maintainer="Martin Zeitler"
WORKDIR "/root"

# Arguments
# TODO: To be changed to `google` upon merging to `main` branch.
# This is merely being used in `clone.sh`, to switch the correct brach.
ARG GITHUB_USER_NAME="syslogic"
ARG GUTHUB_REPO_NAME="android-cuttlefish"

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
ENV REPO_USER="${GITHUB_USER_NAME}"
ENV REPO_NAME="${GUTHUB_REPO_NAME}"

# The EPEL repository installs `dnf-core-plugin` (`config-manager` and `builddeps`) and provides `ncurses-compat-libs`.
# Configuring further package repositories: / https://wiki.rockylinux.org/rocky/repo/#extra-repositories
RUN [ "dnf", "-y", "install", "https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm" ]
RUN [ "rm", "/etc/yum.repos.d/epel-cisco-openh264.repo" ]
RUN [ "dnf", "config-manager", "--set-enabled", "crb" ]
RUN [ "dnf", "-y", "upgrade" ]
RUN [ "dnf", "clean", "all" ]

# https://docs.docker.com/reference/dockerfile/#notes-about-specifying-volumes
# The host directory is declared at container run-time, with `--volume`, eg.
# --volume /home/username/android-cuttlefish/.rpms/:/home/github/.rpms
RUN [ "mkdir", "/home/runner/.rpms" ]
VOLUME [ "/home/runner/.rpms" ]
WORKDIR "/home/runner"

# Host Orchestration
FROM base_image AS orchestration
ENTRYPOINT [ "/home/runner/services.sh" ]

# Expose Operator Port (HTTP:1080, HTTPS:1443)
EXPOSE 1080 1443
# Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
EXPOSE 2080 2443
# Expose WebRTC
EXPOSE 15550-15560
# Expose ADB
EXPOSE 6520-6620

RUN [ "touch", "/home/runner/.dockerenv" ]
RUN [ "echo", "%docker", "ALL=(ALL)", "NOPASSWD:", "ALL", ">", "/etc/sudoers.d/docker.conf" ]
COPY [ "./docker/rpm-builder/scripts/install_rpm.sh", "/home/runner/install_rpm.sh" ]
RUN [ "chmod", "+x", "/home/runner/install_rpm.sh" ]

# Add prebuilt RPM packages.
COPY [ "./.rpms/cuttlefish-base-*", "/home/runner/.rpms/" ]
COPY [ "./.rpms/cuttlefish-orchestration-*", "/home/runner/.rpms/" ]
RUN [ "/bin/bash", "-c", "/home/runner/install_rpm.sh" ]

COPY [ "./docker/rpm-builder/scripts/services.sh", "/home/runner/services.sh" ]
RUN [ "chmod", "+x", "/home/runner/services.sh" ]

# Run commands as user `runner` by default.
USER 1001
