# Virtual Device for Android host-side utilities

This repository holds supporting tools that prepare a host to boot
[Cuttlefish](https://source.android.com/setup/create/cuttlefish), a configurable
Android Virtual Device (AVD) that targets both locally hosted Linux x86/arm64
and remotely hosted Google Compute Engine (GCE) instances rather than physical
hardware.

## Debian packages

The following debian packages are provided:

* `cuttlefish-base` - Creates static resources needed by the Cuttlefish devices
* `cuttlefish-user` - Provides a local web server that enables interactions with
the devices through the browser
* `cuttlefish-integration` - Installs additional utilities to run Cuttlefish in
Google Compute Engine
* `cuttlefish-orchestration` - Replaces `cuttlefish-user` in the
[Orchestration project](https://github.com/google/cloud-android-orchestration)
* `cuttlefish-common` - [DEPRECATED] Provided for compatibility only, it's a
metapackage that depends on `cuttlefish-base` and `cuttlefish-user`

The packages can be built with the following script:

```bash
tools/buildutils/build_packages.sh
```

Cuttlefish requires only `cuttlefish-base` to be installed, but `cuttlefish-user`
is recommended to enjoy a better user experience. These can be installed after
building with the following command:

```bash
sudo apt install ./cuttlefish-base_*.deb ./cuttlefish-user_*.deb
```

## Google Compute Engine

The following script can be used to build a host image for Google Compute Engine:

    device/google/cuttlefish/tools/create_base_image.go

[Check out the AOSP tree](https://source.android.com/setup/build/downloading)
to obtain the script.

## Docker

We also provide the docker image with installed cuttlefish debian packages
inside; including `cuttlefish-base`, `cuttlefish-user`, and
`cuttlefish-orchestration`.
Currently it's available for x86_64 and ARM64 architecture.

### Build docker image manually

Please run below command to build manually.

```bash
cd /path/to/android-cuttlefish
cd docker
./image-builder.sh
```

You can validate if the docker image is successfully built by checking
`cuttlefish-orchestration` in `docker image list` like below.
```
$ docker image list
REPOSITORY               TAG    IMAGE ID       CREATED          SIZE
cuttlefish-orchestration latest 0123456789ab   2 minutes ago    690MB
...
```

### Download prebuilt image

Downloading latest image is available
[here](https://github.com/google/android-cuttlefish/actions/workflows/artifacts.yaml?query=event%3Apush).

After downloading image, please load the image and verify with
`docker image list`.

```bash
docker load --input ${PATH_TO_PREBUILT_DOCKER_IMAGE}
```

Registering the tag of loaded image as `latest` is available with below script.

```bash
docker tag cuttlefish-orchestration:${PREBUILT_DOCKER_IMAGE_TAG} cuttlefish-orchestration:latest
```
