# Build debian packages in containers

**Podman Compatible**: packages can be built with [Podman](https://podman.io) as well.

## Build the image

The build image command must be run at the root of the `android-cuttlefish` repo directory.

Enabling Docker [BuildKit](https://docs.docker.com/build/buildkit/) is required
on Docker version below 23.0 to build this image.

```
docker build \
  --file "tools/buildutils/cw/Containerfile" \
  --tag "android-cuttlefish-build:latest" \
  .
```

## Build the package

The run container command must be run at the root of the `android-cuttlefish` repo directory.

### base

```
docker run -v=$PWD:/mnt/build -w /mnt/build android-cuttlefish-build:latest base
```

Persist bazel cache among executions.

```
docker run -v=$HOME/.cache/bazel:/root/.cache/bazel  -v=$PWD:/mnt/build -w /mnt/build  android-cuttlefish-build:latest base
```

### frontend

```
docker run -v=$PWD:/mnt/build -w /mnt/build android-cuttlefish-build:latest frontend
```
