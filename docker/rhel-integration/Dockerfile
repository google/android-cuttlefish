# RHEL Integration Dockerfile based on `rockylinux/rockylinux:9.5`
ARG LINUX_IMAGE=rockylinux/rockylinux:9.5
FROM $LINUX_IMAGE AS base_image
LABEL description="RHEL Integration"
LABEL version="1.0.2"
LABEL repository="https://github.com/google/android-cuttlefish"
LABEL maintainer="Martin Zeitler"
SHELL [ "/bin/bash", "-c" ]
WORKDIR "/root"
USER "root"

# Arguments
ARG RUNNER_GID="118"
ARG RUNNER_UID="1001"
ENV GID="${RUNNER_GID}"
ENV UID="${RUNNER_UID}"

# Adding the user provides the home directory.
RUN groupadd -f -g ${GID} docker
RUN useradd -g ${GID} -u ${UID} -s /bin/bash runner

# Don't ask for the `sudo` password.
RUN [ "echo", "%docker", "ALL=(ALL)", "NOPASSWD:", "ALL", ">", "/etc/sudoers.d/docker.conf" ]

# The EPEL repository installs `dnf-core-plugin` (`config-manager` and `builddeps`) and provides `ncurses-compat-libs`.
# Configuring further package repositories: / https://wiki.rockylinux.org/rocky/repo/#extra-repositories
RUN [ "dnf", "-y", "install", "sudo", "nano", "wget", "https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm"]
RUN [ "rm", "/etc/yum.repos.d/epel-cisco-openh264.repo" ]
RUN [ "dnf", "config-manager", "--set-enabled", "crb" ]
RUN [ "dnf", "-y", "upgrade" ]
RUN [ "dnf", "clean", "all" ]

# Import Google RPM repository public key.
# https://www.google.com/linuxrepositories/
# RUN [ "wget", "https://dl.google.com/linux/linux_signing_key.pub" ]
# RUN [ "rpm", "--import", "linux_signing_key.pub" ]


# Integration
FROM base_image AS integration
ARG ARTIFACT_PATH=./.rpms

# Provide an interactive shell.
ENTRYPOINT [ "/bin/bash" ]

# Expose Operator Port (HTTP:1080, HTTPS:1443)
EXPOSE 1080 1443

# Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
EXPOSE 2080 2443

# Expose WebRTC
EXPOSE 15550-15560

# Expose ADB
EXPOSE 6520-6620

# Restore build artifacts.
RUN [ "mkdir", "/root/.rpms" ]
RUN [ "touch", "/root/.dockerenv", "/home/runner/.dockerenv" ]
COPY [ "${ARTIFACT_PATH}/cuttlefish-integration-*", "/root/.rpms" ]
COPY [ "${ARTIFACT_PATH}/cuttlefish-user-*", "/root/.rpms/" ]
COPY [ "${ARTIFACT_PATH}/cuttlefish-base-*", "/root/.rpms/" ]

# Container scripts.
COPY [ "./docker/rpm-builder/scripts/install_rpm.sh", "/root/install_rpm.sh" ]
RUN [ "chmod", "+x", "/root/install_rpm.sh" ]

# Install RPM.
RUN [ "/bin/bash", "-c", "/root/install_rpm.sh" ]
RUN [ "dnf", "clean", "all" ]

# Run commands as user `runner`.
WORKDIR "/home/runner"
USER "runner"
