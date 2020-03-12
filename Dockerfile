# This file is based on https://hub.docker.com/r/jrei/systemd-debian/.

FROM debian:buster-slim

ENV container docker
ENV LC_ALL C
ENV DEBIAN_FRONTEND noninteractive

RUN apt-get update \
    && apt-get install -y systemd \
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

RUN apt update \
    && apt install -y apt-utils sudo vim dpkg-dev devscripts gawk coreutils \
       openssh-server openssh-client psmisc iptables iproute2 dnsmasq \
       net-tools rsyslog equivs # qemu-system-x86

RUN apt install -y dialog


SHELL ["/bin/bash", "-c"]

RUN if test $(uname -m) == aarch64; then \
	    dpkg --add-architecture amd64 \
	    && apt update \
	    && apt install -y libc6:amd64 \
	    && apt install -y qemu qemu-user qemu-user-static binfmt-support; \
    fi

RUN if test $(uname -m) == x86_64; then \
	apt install -y pciutils; \
	if test $(lspci | grep -i vga | grep -icw nvidia) -gt 0; then \
		apt install -y software-properties-common; \
		apt-add-repository contrib; \
		apt-add-repository non-free; \
		apt update; \
		apt install -y nvidia-detect; \
		nvidia-detect; \
		apt install -y nvidia-driver; \
	fi; \
    fi

COPY . /root/android-cuttlefish/

RUN cd /root/android-cuttlefish \
    && yes | sudo mk-build-deps -i -r -B \
    && dpkg-buildpackage -uc -us \
    && apt install -y -f ../cuttlefish-common_*.deb

RUN apt-get clean \
    && rm -rf /root/android-cuttlefish

RUN useradd -ms /bin/bash vsoc-01 -d /home/vsoc-01 -G kvm,cvdnetwork \
    && passwd -d vsoc-01 \
    && echo 'vsoc-01 ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

RUN sed -i -r -e 's/^#{0,1}\s*PasswordAuthentication\s+(yes|no)/PasswordAuthentication yes/g' /etc/ssh/sshd_config \
    && sed -i -r -e 's/^#{0,1}\s*PermitEmptyPasswords\s+(yes|no)/PermitEmptyPasswords yes/g' /etc/ssh/sshd_config \
    && sed -i -r -e 's/^#{0,1}\s*ChallengeResponseAuthentication\s+(yes|no)/ChallengeResponseAuthentication no/g' /etc/ssh/sshd_config \
    && sed -i -r -e 's/^#{0,1}\s*UsePAM\s+(yes|no)/UsePAM no/g' /etc/ssh/sshd_config
