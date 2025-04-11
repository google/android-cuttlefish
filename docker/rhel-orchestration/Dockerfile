# Dockerfile based on `rockylinux/rockylinux:9.5`
ARG LINUX_IMAGE=rockylinux/rockylinux:9.5
FROM $LINUX_IMAGE AS base_image
LABEL description="RHEL Orchestration"
LABEL version="1.0.2"
LABEL repository="https://github.com/google/android-cuttlefish"
LABEL maintainer="Martin Zeitler"
SHELL [ "/bin/bash", "-c" ]

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
RUN [ "dnf", "-y", "install", "https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm" ]
RUN [ "rm", "/etc/yum.repos.d/epel-cisco-openh264.repo" ]
RUN [ "dnf", "config-manager", "--set-enabled", "crb" ]
RUN [ "dnf", "-y", "upgrade" ]
RUN [ "dnf", "clean", "all" ]


# Host Orchestration
FROM base_image AS orchestration

# Required to keep the container running.
ENTRYPOINT [ "/home/runner/services.sh" ]

# Expose Operator Port (HTTP:1080, HTTPS:1443)
EXPOSE 1080 1443

# Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
EXPOSE 2080 2443

# Expose WebRTC
EXPOSE 15550-15560

# Expose ADB
EXPOSE 6520-6620

# https://docs.docker.com/reference/dockerfile/#notes-about-specifying-volumes
# The host directory is declared at container run-time, with `--volume`, eg.
# --volume /home/username/android-cuttlefish/.rpms/:/home/github/.rpms
RUN [ "mkdir", "/home/runner/.rpms" ]
VOLUME [ "/home/runner/.rpms" ]
WORKDIR "/home/runner"

# Import Google RPM repository public key.
# https://www.google.com/linuxrepositories/
RUN [ "dnf", "-y", "install", "wget"]
RUN [ "wget", "https://dl.google.com/linux/linux_signing_key.pub" ]
RUN [ "rpm", "--import", "linux_signing_key.pub" ]

# Add prebuilt RPM packages and install them.
RUN [ "touch", "/home/runner/.dockerenv" ]
COPY [ "./.rpms/cuttlefish-base-*", "/home/runner/.rpms/" ]
COPY [ "./.rpms/cuttlefish-orchestration-*", "/home/runner/.rpms/" ]

RUN for FILE in /home/runner/.rpms/* ; do dnf -y --skip-broken install $FILE; done
RUN [ "dnf", "clean", "all" ]

# Copy services.sh
COPY [ "./docker/rhel-orchestration/services.sh", "/home/runner/services.sh" ]
RUN [ "chmod", "+x", "/home/runner/services.sh" ]

# Run commands as user `runner`, who is the owner of the RPM packages.
USER "runner"
