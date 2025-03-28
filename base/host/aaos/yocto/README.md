# Building Yocto in a Podman

Yocto allows for highly-customized distros, like Gentoo. It is popular in the automotive space.  This directory contains scripts to enable reproducible, containerized builds for certain use cases.

This assumes a Debian system, and uses a Debian container.

## Install Podman, enable rootless
Podman should be run in rootless mode.

```
sudo apt-get -y install podman
sudo usermod --add-subuids 100000-165535 --add-subgids 100000-165535 $(whoami)
podman system migrate
```

## Build the container
Podman should be run in rootless mode.

```
podman build --tag yocto-on-debian .
podman run -t -i yocto-on-debian
podman run --memory 0 --memory-swap -1 -ti --name mybuild yocto-on-debian
```

## Container management

```
podman start mybuild
podman exec -ti mybuild /bin/bash
podman stop mybuild
podman ps --all
podman rm <CONTAINER_ID>
```
