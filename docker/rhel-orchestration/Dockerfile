# RHEL Orchestration Dockerfile
ARG LINUX_IMAGE=rockylinux/rockylinux:9.5
FROM $LINUX_IMAGE AS base_image
LABEL description="RHEL Orchestration"
LABEL version="1.0.2"
LABEL repository="https://github.com/google/android-cuttlefish"
LABEL maintainer="Martin Zeitler"
RUN [ "touch", "/root/.dockerenv" ]
SHELL [ "/bin/bash", "-c" ]
WORKDIR "/root"
USER "root"

# The EPEL repository installs `dnf-core-plugin` (`config-manager` and `builddeps`) and provides `ncurses-compat-libs`.
# Configuring further package repositories: / https://wiki.rockylinux.org/rocky/repo/#extra-repositories
RUN [ "dnf", "-y", "install", "epel-release" ]
RUN [ "rm", "/etc/yum.repos.d/epel-cisco-openh264.repo" ]
RUN [ "dnf", "config-manager", "--set-enabled", "crb" ]
RUN [ "dnf", "-y", "upgrade" ]
RUN [ "dnf", "clean", "all" ]


# Host Orchestration
FROM base_image AS orchestration
ARG ARTIFACT_PATH=./.rpms

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
COPY [ "${ARTIFACT_PATH}/cuttlefish-orchestration-*", "/root/.rpms" ]
COPY [ "${ARTIFACT_PATH}/cuttlefish-base-*", "/root/.rpms" ]

# Container scripts.
COPY [ "./docker/rpm-builder/scripts/install_rpm.sh", "/root/install_rpm.sh" ]
RUN [ "chmod", "+x", "/root/install_rpm.sh" ]

# Install RPM.
RUN [ "/bin/bash", "-c", "/root/install_rpm.sh" ]
RUN [ "dnf", "clean", "all" ]

# Required to keep the container running.
COPY [ "./docker/rhel-orchestration/services.sh", "/root/services.sh" ]
RUN [ "chmod", "+x", "/root/services.sh" ]
ENTRYPOINT [ "/root/services.sh" ]
