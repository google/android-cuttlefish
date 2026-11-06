# Build debian packages in containers

**Podman Compatible**: packages can be built with [Podman](https://podman.io) as well.

## Build the image

The build image command must be run at the root of the `android-cuttlefish` repo directory.

Enabling Docker [BuildKit](https://docs.docker.com/build/buildkit/) is required
on Docker version below 23.0 to build this image.

```bash
docker build \
  --file "tools/buildutils/cw/Containerfile" \
  --tag "android-cuttlefish-build:latest" \
  .
```

The `cuttlefish-podcvd` package (built with `container` below) must be built on
Debian 13 (trixie). Build another image with the `BASE_IMAGE` build argument
for it.

```bash
docker build \
  --file "tools/buildutils/cw/Containerfile" \
  --build-arg BASE_IMAGE=mirror.gcr.io/library/debian:13 \
  --tag "android-cuttlefish-build-trixie:latest" \
  .
```

## Build the package

The run container command must be run at the root of the `android-cuttlefish` repo directory.

### base

```bash
docker run -v=$PWD:/mnt/build -w /mnt/build android-cuttlefish-build:latest base
```

Persist bazel cache among executions.

```bash
docker run -v=$HOME/.cache/bazel:/root/.cache/bazel  -v=$PWD:/mnt/build -w /mnt/build  android-cuttlefish-build:latest base
```

### container

Builds `cuttlefish-podcvd`.

```bash
docker run -v=$PWD:/mnt/build -w /mnt/build android-cuttlefish-build-trixie:latest container
```

Use a custom container image for `podcvd`, built with podman as the user running
`podcvd` (refer to
[`container/README.md`](../../../container/README.md#build-container-image-manually)).

```bash
container/image/image-builder.sh -m dev -c podman -t <image>
docker run -e container_image_name=<image> -v=$PWD:/mnt/build -w /mnt/build android-cuttlefish-build-trixie:latest container
```

### frontend

```bash
docker run -v=$PWD:/mnt/build -w /mnt/build android-cuttlefish-build:latest frontend
```
