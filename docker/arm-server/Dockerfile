FROM ubuntu:22.04

EXPOSE 15550-15560
EXPOSE 1443
EXPOSE 6520-6620
USER root

RUN set -x

RUN apt update
RUN apt install -y --no-install-recommends git
RUN apt install -y --no-install-recommends ca-certificates && update-ca-certificates
RUN apt install -y --no-install-recommends dpkg-dev build-essential:native config-package-dev debhelper-compat golang-1.13 libprotobuf-dev protobuf-compiler curl
RUN apt install -y --no-install-recommends gcc-10

WORKDIR /root
RUN git clone https://github.com/google/android-cuttlefish.git
WORKDIR /root/android-cuttlefish/base
RUN dpkg-buildpackage
WORKDIR /root/android-cuttlefish/frontend
RUN dpkg-buildpackage

WORKDIR /root/android-cuttlefish
RUN apt install -y -f ./cuttlefish-base_*.deb ./cuttlefish-user_*.deb ./cuttlefish-orchestration_*.deb
RUN echo "num_cvd_accounts=100" >> /etc/default/cuttlefish-host-resources

WORKDIR /root

RUN usermod -aG kvm root
RUN usermod -aG cvdnetwork root

COPY ./run_services.sh /run_services.sh
RUN chmod +x /run_services.sh

ENTRYPOINT ["/run_services.sh"]
