# This file is based on https://hub.docker.com/r/jrei/systemd-debian/.

FROM debian:buster

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
       net-tools rsyslog qemu-system-x86 equivs

COPY . /root/android-cuttlefish/

RUN cd /root/android-cuttlefish \
    && yes | sudo mk-build-deps -i -r -B \
    && dpkg-buildpackage -uc -us \
    && apt install -y -f ../cuttlefish-common_*_amd64.deb

RUN apt-get clean \
    && rm -rf /root/android-cuttlefish

RUN groupadd kvm

RUN useradd -ms /bin/bash vsoc-01 -d /home/vsoc-01 -G kvm,cvdnetwork \
    && passwd -d vsoc-01 \
    && echo 'vsoc-01 ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers \
    && echo 'sudo chmod ug+rw /dev/kvm' >> /home/vsoc-01/.bashrc \
    && echo 'sudo chmod ug+rw /dev/vhost-vsock' >> /home/vsoc-01/.bashrc \
    && echo 'sudo chown root.kvm /dev/kvm' >> /home/vsoc-01/.bashrc \
    && echo 'sudo chown root.cvdnetwork /dev/vhost-vsock' >> /home/vsoc-01/.bashrc

RUN sed -i -r -e 's/^#{0,1}\s*PasswordAuthentication\s+(yes|no)/PasswordAuthentication yes/g' /etc/ssh/sshd_config \
    && sed -i -r -e 's/^#{0,1}\s*PermitEmptyPasswords\s+(yes|no)/PermitEmptyPasswords yes/g' /etc/ssh/sshd_config \
    && sed -i -r -e 's/^#{0,1}\s*ChallengeResponseAuthentication\s+(yes|no)/ChallengeResponseAuthentication no/g' /etc/ssh/sshd_config \
    && sed -i -r -e 's/^#{0,1}\s*UsePAM\s+(yes|no)/UsePAM no/g' /etc/ssh/sshd_config
