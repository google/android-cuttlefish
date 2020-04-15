# This file is based on https://hub.docker.com/r/jrei/systemd-debian/.

FROM debian:buster-slim AS cuttlefish-softgpu

ENV container docker
ENV LC_ALL C
ENV DEBIAN_FRONTEND noninteractive

# Set up the user have the same UID as user creating the container.  This is
# important when we map the (container) user's home directory as a docker volume.

ARG UID

USER root
WORKDIR /root

RUN set -x

RUN apt-get update \
    && apt-get install --no-install-recommends -y systemd \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* \
    && rm -f /var/run/nologin

RUN rm -f /lib/systemd/system/multi-user.target.wants/* \
    /etc/systemd/system/*.wants/* \
    /lib/systemd/system/local-fs.target.wants/* \
    /lib/systemd/system/sockets.target.wants/*udev* \
    /lib/systemd/system/sockets.target.wants/*initctl* \
    /lib/systemd/system/sysinit.target.wants/systemd-tmpfiles-setup* \
    /lib/systemd/system/systemd-update-utmp*

VOLUME [ "/sys/fs/cgroup" ]

CMD ["/lib/systemd/systemd"]

RUN apt-get update \
    && apt-get install --no-install-recommends -y apt-utils sudo vim dpkg-dev devscripts gawk coreutils \
       openssh-server openssh-client psmisc iptables iproute2 dnsmasq \
       net-tools rsyslog equivs # qemu-system-x86

RUN apt-get install --no-install-recommends -y dialog

SHELL ["/bin/bash", "-c"]

RUN if test $(uname -m) == aarch64; then \
	    dpkg --add-architecture amd64 \
	    && apt-get update \
	    && apt-get install --no-install-recommends -y libc6:amd64 \
	    && apt-get install --no-install-recommends -y qemu qemu-user qemu-user-static binfmt-support; \
    fi

COPY . android-cuttlefish/

RUN cd /root/android-cuttlefish \
    && yes | sudo mk-build-deps -i -r -B \
    && dpkg-buildpackage -uc -us \
    && apt-get install --no-install-recommends -y -f ../cuttlefish-common_*.deb \
    && rm -rvf ../cuttlefish-common_*.deb \
    && cd .. \
    && rm -rvf android-cuttlefish

RUN apt-get install -y curl wget unzip

RUN apt-get clean

RUN useradd -ms /bin/bash vsoc-01 -d /home/vsoc-01 -u $UID -G kvm,cvdnetwork \
    && passwd -d vsoc-01 \
    && echo 'vsoc-01 ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

RUN sed -i -r -e 's/^#{0,1}\s*PasswordAuthentication\s+(yes|no)/PasswordAuthentication yes/g' /etc/ssh/sshd_config \
    && sed -i -r -e 's/^#{0,1}\s*PermitEmptyPasswords\s+(yes|no)/PermitEmptyPasswords yes/g' /etc/ssh/sshd_config \
    && sed -i -r -e 's/^#{0,1}\s*ChallengeResponseAuthentication\s+(yes|no)/ChallengeResponseAuthentication no/g' /etc/ssh/sshd_config \
    && sed -i -r -e 's/^#{0,1}\s*UsePAM\s+(yes|no)/UsePAM no/g' /etc/ssh/sshd_config

WORKDIR /home/vsoc-01

COPY --chown=vsoc-01 download-aosp.sh .

VOLUME [ "/home/vsoc-01" ]

FROM cuttlefish-softgpu AS cuttlefish-hwgpu

# RUN apt-get upgrade -y

ARG OEM

WORKDIR /root

RUN pushd android-cuttlefish; \
    gpu/${OEM}/prep.sh; \
    echo "### INSTALLING STUB DEPENDENCIES"; \
    cat gpu/${OEM}/driver-deps/equivs.txt | while read -e NAME VER OP; do \
      echo "### INSTALL STUB ${NAME} ${VER}"; \
      ./equivs.sh "${NAME}" "${VER//:/%3a}" "${OP}"; \
    done; \
    echo "### DONE INSTALLING STUB DEPENDENCIES"; \
    echo "### INSTALLING DEPENDENCIES"; \
    cat gpu/${OEM}/driver.txt | while read -e NAME VER; do \
      if [ -z "${VER}" ]; then \
        VER=_; \
      fi; \
      echo "### INSTALL ${NAME} ${VER}"; \
      ./install-deps.sh _ _ "${NAME}" "${VER}" eq "gpu/${OEM}/filter-in-deps.sh" ./install-deps.sh "gpu/${OEM}/driver-deps"; \
    done; \
    echo "### DONE INSTALLING DEPENDENCIES"; \
    dpkg -C; \
    popd

WORKDIR /home/vsoc-01
