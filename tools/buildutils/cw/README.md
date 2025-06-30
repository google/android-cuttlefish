# Build debian packages in containers

**Podman Compatible**: packages can be built with [Podman](https://podman.io) as well.

## Build the image

The build image command must be run at the root of the `android-cuttlefish` repo directory.

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
