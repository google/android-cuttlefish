# Docker image for running CF instances in ARM64 server.
# Docker image includes HO(Host Orchestrator) inside,
# so it could execute CF instance with API in HO.
FROM ubuntu:22.04 AS cuttlefish-arm64

# Expose Operator Port (HTTP:1080, HTTPS:1443)
EXPOSE 1080 1443
# Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
EXPOSE 2080 2443
# Expose WebRTC Port
EXPOSE 15550-15560
# Expose ADB Port
# Corresponding ADB port for CF instance is, 6520+instance_num-1.
EXPOSE 6520-6620

USER root

RUN set -x

# Install dependants of CF debian packages.
RUN apt update
RUN apt install -y --no-install-recommends \
    ca-certificates \
    curl \
    sudo
RUN update-ca-certificates

# ADD CF debian packages built by
# docker/debs-builder-docker/build-debs-with-docker.sh.
COPY ./out/cuttlefish-base_*.deb /root/
COPY ./out/cuttlefish-user_*.deb /root/
COPY ./out/cuttlefish-orchestration_*.deb /root/

# Install CF debian packages.
WORKDIR /root
RUN apt install -y -f \
    /root/cuttlefish-base_*.deb \
    /root/cuttlefish-user_*.deb \
    /root/cuttlefish-orchestration_*.deb

RUN echo "num_cvd_accounts=100" >> /etc/default/cuttlefish-host-resources

RUN usermod -aG kvm root
RUN usermod -aG cvdnetwork root

COPY ./run_services.sh /run_services.sh
RUN chmod +x /run_services.sh

ENTRYPOINT ["/run_services.sh"]
