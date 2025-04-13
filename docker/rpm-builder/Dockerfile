# RPM Builder Dockerfile based on `rockylinux/rockylinux:9.5`
ARG LINUX_IMAGE=rockylinux/rockylinux:9.5
FROM $LINUX_IMAGE AS base_image
LABEL description="RPM Builder"
LABEL version="1.0.2"
LABEL repository="https://github.com/google/android-cuttlefish"
LABEL maintainer="Martin Zeitler"
SHELL [ "/bin/bash", "-c" ]

# Arguments
# This is being used by `scripts/clone.sh`, to switch the branch.
ARG GITHUB_USER="google"
ARG GUTHUB_REPO="android-cuttlefish"
ARG GITHUB_BRANCH="main"

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

# To be supplied at runtime.
ENV RPM_SIGN_SIGNATURE=""
ENV RPM_SIGN_PASSPHRASE=""

# Run the build script.
ENTRYPOINT [ "/bin/bash", "-c", "/home/runner/build.sh", "${REPO_USER}", "${REPO_NAME}", "${REPO_BRANCH}" ]

# Build dependencies.
RUN [ "dnf", "-y", "install", "git", "cmake", "gcc-c++", "rpm-build", "golang-bin", "expect", "vim-common", "ncurses-compat-libs", "curl-devel", "wayland-devel", "libaom-devel", "opus-devel", "libzip-devel", "libzstd-devel", "protobuf-devel", "protobuf-compiler" ]
RUN [ "dnf", "-y", "upgrade" ]
RUN [ "dnf", "clean", "all" ]

# Install `bazelisk` for all users.
RUN if [ "$(uname -i)" =  "x86_64" ]; then wget --no-verbose --output-document /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/download/v1.25.0/bazelisk-linux-amd64; fi
RUN if [ "$(uname -i)" = "aarch64" ]; then wget --no-verbose --output-document /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/download/v1.25.0/bazelisk-linux-arm64; fi
RUN [ "chmod", "+x", "/usr/local/bin/bazel" ]

# Container scripts.
RUN [ "touch", "/root/.dockerenv", "/home/runner/.dockerenv" ]
RUN [ "cp", "/root/.bash_profile", "/home/runner/.bash_profile" ]
COPY [ "./docker/rpm-builder/scripts/build.sh", "/home/runner/build.sh" ]
COPY [ "./docker/rpm-builder/scripts/clone.sh", "/home/runner/clone.sh" ]
COPY [ "./docker/rpm-builder/scripts/sign_rpm.sh", "/home/runner/sign_rpm.sh" ]
COPY [ "./docker/rpm-builder/scripts/sign_rpm.exp", "/usr/local/bin/sign_rpm.exp" ]
COPY [ "./docker/rpm-builder/scripts/install_rpm.sh", "/home/runner/install_rpm.sh" ]
COPY [ "./docker/rpm-builder/scripts/patch_zlib.sh", "/home/runner/patch_zlib.sh" ]
COPY [ "./docker/rpm-builder/build_rpm_spec.sh", "/home/runner/build_rpm_spec.sh" ]

RUN [ "echo", "export", "GOPATH=/home/runner/go", ">>", "/home/runner/.bash_profile" ]
RUN [ "echo", "export", "PATH=$PATH:/home/runner/go/bin", ">>", "/home/runner/.bash_profile" ]
RUN [ "echo", "export", "BUILDX_GIT_LABELS=1", ">>", "/home/runner/.bash_profile" ]

RUN [ "chmod", "+x", "/home/runner/build_rpm_spec.sh", "/home/runner/patch_zlib.sh", "/home/runner/install_rpm.sh" ]
RUN [ "chmod", "+x", "/home/runner/sign_rpm.sh", "/usr/local/bin/sign_rpm.exp" ]
RUN [ "chmod", "+x", "/home/runner/build.sh", "/home/runner/clone.sh" ]
RUN [ "chown", "-R", "runner:docker", "/home/runner" ]

# Run commands as by default as user `runner`,
# else the files in the bind-mounts will be owned by user `root`.
USER "runner"
WORKDIR "/home/runner"
RUN source ~/.bash_profile


# RPM Testing
FROM builder AS testing
ENTRYPOINT [ "/bin/bash" ]
