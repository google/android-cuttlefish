# Build debian packages in containers

## Build the image

The build image command must be run at the root of the `android-cuttlefish` repo directory.

```
podman build \
  --file "tools/buildutils/cw/Containerfile" \
  --tag "android-cuttlefish-build:latest" \
  .
```


## Build the package

The run container command must be run at the root of the `android-cuttlefish` repo directory.

### base

```
podman run -v=.:/mnt/build -w /mnt/build/base android-cuttlefish-build:latest
```

Persist bazel cache among executions.

```
podman run -v=$HOME/.cache/bazel:/root/.cache/bazel  -v=.:/mnt/build -w /mnt/build/base  android-cuttlefish-build:latest
```

### frontend

```
podman run -v=.:/mnt/build -w /mnt/build/frontend android-cuttlefish-build:latest
```
