FROM debian:buster-slim AS cuttlefish-android-builder

ENV container docker
ENV LC_ALL C
ENV DEBIAN_FRONTEND noninteractive

# Set up the user have the same UID as user creating the container.  This is
# important when we map the (container) user's home directory as a docker volume.

ARG UID

USER root
WORKDIR /root

RUN set -x

RUN apt-get update -y
RUN apt-get install -y python python3 wget curl git build-essential libncurses5 libncurses5-dev zip subversion rsync
RUN update-alternatives --install /usr/bin/python python /usr/bin/python3 1
RUN mkdir /repo-bin && curl https://storage.googleapis.com/git-repo-downloads/repo > /repo-bin/repo
RUN chmod a+x /repo-bin/repo

ENV PATH=$PATH:/repo-bin

RUN apt-get install --no-install-recommends -y apt-utils sudo vim dpkg-dev devscripts gawk coreutils
RUN apt-get install -y procps
SHELL ["/bin/bash", "-c"]
RUN if test $(uname -m) == aarch64; then \
	    dpkg --add-architecture amd64 \
	    && apt-get update \
	    && apt-get install --no-install-recommends -y libc6:amd64 \
	    && apt-get install --no-install-recommends -y qemu qemu-user qemu-user-static binfmt-support; \
    fi

RUN useradd -ms /bin/bash vsoc-01 -d /home/vsoc-01 -u $UID \
    && passwd -d vsoc-01 \
    && echo 'vsoc-01 ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

WORKDIR /home/vsoc-01

RUN mkdir -p /home/vsoc-01/intrinsic_shells/bin
COPY ./common_intrinsic.sh /home/vsoc-01/intrinsic_shells/bin/
COPY ./run.sh /home/vsoc-01/intrinsic_shells/bin/

RUN chmod +x /home/vsoc-01/intrinsic_shells/bin/*.sh

# use sudo if root privilege is needed
USER vsoc-01
VOLUME [ "/home/vsoc-01" ]
