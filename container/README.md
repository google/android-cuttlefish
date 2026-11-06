# Container images

We provide container images with installed cuttlefish debian packages inside;
including `cuttlefish-base`, `cuttlefish-user`, and `cuttlefish-orchestration`.
The images are based on Debian 13 (trixie), and available for x86_64 and ARM64
architectures.

[`podcvd`](src/podcvd/README.md) runs each Cuttlefish instance group in a
container created from this image.

## Download container image

Container images are available to download from Artifact Registry. Each image
is tagged by release channel (`stable`, `unstable`, `nightly`) and by version.
Please run command below to download latest version of container image from the
stable channel.

```bash
docker pull us-docker.pkg.dev/android-cuttlefish-artifacts/cuttlefish-orchestration/cuttlefish-orchestration:stable
```

## Use container image with Cloud Orchestrator

Please refer to
[Cloud Orchestrator documentation for on-premise server](https://github.com/google/cloud-android-orchestration/blob/main/scripts/on-premises/single-server/README.md).

## Build container image manually

Use `container/image/image-builder.sh` to build container image. Run
`container/image/image-builder.sh -h` to see all options.

| Option | Description |
|---|---|
| `-m <mode>` | `stable`, `unstable`, `nightly`: install host packages from the given release channel (default: `stable`)<br>`dev`: install `*.deb` files under the repository root directory |
| `-c <container_type>` | `docker` or `podman` (default: `docker`) |
| `-t <tag>` | Name or name:tag of container image (default: `cuttlefish-orchestration`) |
| `-M` | Install `cuttlefish-metrics` additionally |

To build container image with `dev` mode, building host debian packages
including `base` and `frontend` is required in advance. Please refer to
[tools/buildutils/cw/README.md](../tools/buildutils/cw/README.md) for building
them, and keep the built `*.deb` files under the repository root directory.

### Docker

Please run below command to build docker image manually.

```bash
cd /path/to/android-cuttlefish
container/image/image-builder.sh -m dev -c docker
```

You can validate if the docker image is successfully built by checking
`cuttlefish-orchestration` in `docker image list` like below.
```
$ docker image list
REPOSITORY               TAG    IMAGE ID       CREATED          SIZE
cuttlefish-orchestration latest 0123456789ab   2 minutes ago    690MB
...
```

### Podman

Please run below command to build podman image manually.

```bash
cd /path/to/android-cuttlefish
container/image/image-builder.sh -m dev -c podman
```

You can validate if the podman image is successfully built by checking
`cuttlefish-orchestration` in `podman image list` like below.
```
$ podman image list
REPOSITORY                            TAG                IMAGE ID      CREATED         SIZE
localhost/cuttlefish-orchestration    latest             b5870005843b  39 minutes ago  1.12 GB
...
```
