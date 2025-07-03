# Docker

We provide docker images with installed cuttlefish debian packages inside;
including `cuttlefish-base`, `cuttlefish-user`, and `cuttlefish-orchestration`.
Currently it's available for x86_64 and ARM64 architectures.

## Download docker image

Currently docker image is available to download from Artifact Registry.
Please run command below to download latest version of docker image.

Also, please choose one location among `us`, `europe`, or `asia`.
It's available to download artifacts from any location, but download latency is
different based on your location.

```bash
DOWNLOAD_LOCATION=us # Choose one among us, europe, or asia.
docker pull $DOWNLOAD_LOCATION-docker.pkg.dev/android-cuttlefish-artifacts/cuttlefish-orchestration/cuttlefish-orchestration
```

## Use docker image with Cloud Orchestrator

Please refer to
[Cloud Orchestrator documentation for on-premise server](https://github.com/google/cloud-android-orchestration/blob/main/scripts/on-premises/single-server/README.md).

## Build docker image manually

To build docker image, building host debian packages for docker image is
required. Please refer to
[tools/buildutils/cw/README.md](../tools/buildutils/cw/README.md) for building
host debian packages including `base` and `frontend`.

After retrieving host debian packages, please run below command to build
manually.

```bash
cd /path/to/android-cuttlefish
docker/image-builder.sh -m dev
```

You can validate if the docker image is successfully built by checking
`cuttlefish-orchestration` in `docker image list` like below.
```
$ docker image list
REPOSITORY               TAG    IMAGE ID       CREATED          SIZE
cuttlefish-orchestration latest 0123456789ab   2 minutes ago    690MB
...
```
