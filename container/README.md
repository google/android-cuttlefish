# Container images

We provide container images with installed cuttlefish debian packages inside;
including `cuttlefish-base`, `cuttlefish-user`, and `cuttlefish-orchestration`.
Currently it's available for x86_64 and ARM64 architectures.

## Download docker image

Currently docker image is available to download from Artifact Registry.
Please run command below to download latest version of docker image.

```bash
docker pull us-docker.pkg.dev/android-cuttlefish-artifacts/cuttlefish-orchestration/cuttlefish-orchestration:stable
```

## Use docker image with Cloud Orchestrator

Please refer to
[Cloud Orchestrator documentation for on-premise server](https://github.com/google/cloud-android-orchestration/blob/main/scripts/on-premises/single-server/README.md).

## Build container image manually

To build container image, building host debian packages is required in
advance.
Please refer to
[tools/buildutils/cw/README.md](../tools/buildutils/cw/README.md) for building
host debian packages including `base` and `frontend`.

### Docker

Please run below command to build docker image manually.

```bash
cd /path/to/android-cuttlefish
container/image-builder.sh -m dev -c docker
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
sudo container/image-builder.sh -m dev -c podman
```

You can validate if the podman image is successfully built by checking
`cuttlefish-orchestration` in `sudo podman image list` like below.
```
$ sudo podman image list
REPOSITORY                            TAG                IMAGE ID      CREATED         SIZE
localhost/cuttlefish-orchestration    latest             b5870005843b  39 minutes ago  1.12 GB
...
```
