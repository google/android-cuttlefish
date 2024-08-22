# Docker image for running CF instances in the server.
# Docker image includes HO(Host Orchestrator) inside,
# so it could execute CF instance with API in HO.

FROM debian:12 AS cuttlefish-hostpkg-builder

USER root
WORKDIR /root
RUN set -x
RUN apt update
RUN apt install -y --no-install-recommends \
    apt-utils \
    curl \
    devscripts \
    equivs \
    sudo

# Build CF debian packages 
COPY . /root/android-cuttlefish
RUN ["/bin/bash", "-c", "/root/android-cuttlefish/tools/buildutils/build_packages.sh"]

FROM debian:12 AS cuttlefish-orchestration

# Expose Operator Port (HTTP:1080, HTTPS:1443)
EXPOSE 1080 1443
# Expose HO(Host Orchestrator) Port (HTTP:2080, HTTPS:2443)
EXPOSE 2080 2443
# Expose WebRTC Port
EXPOSE 15550-15560
# Expose ADB Port
# Corresponding ADB port for CF instance is, 6520+instance_num-1.
EXPOSE 6520-6620
# to make sure this file always exist
RUN touch /.dockerenv

USER root
WORKDIR /root
RUN set -x
RUN apt update
RUN apt install -y --no-install-recommends \
    ca-certificates \
    curl \
    nginx \
    sudo
RUN update-ca-certificates

# Install CF debian packages.
COPY --from=cuttlefish-hostpkg-builder /root/android-cuttlefish/cuttlefish-*.deb /root/debian/
RUN apt install -y --no-install-recommends -f \
    /root/debian/cuttlefish-base_*.deb \
    /root/debian/cuttlefish-user_*.deb \
    /root/debian/cuttlefish-orchestration_*.deb

RUN echo "num_cvd_accounts=100" >> /etc/default/cuttlefish-host-resources

RUN usermod -aG kvm root
RUN usermod -aG cvdnetwork root

COPY --from=cuttlefish-hostpkg-builder /root/android-cuttlefish/docker/guest/run_services.sh /root/
RUN chmod +x /root/run_services.sh
RUN rm -rf /root/android-cuttlefish
ENTRYPOINT ["/root/run_services.sh"]
