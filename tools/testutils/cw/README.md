# Run e2e tests in Containers

## Set the relevant permissions


```
sudo setfacl -m "u:$(whoami):rw" /dev/kvm
sudo setfacl -m "u:$(whoami):rw" /dev/vhost-net
sudo setfacl -m "u:$(whoami):rw" /dev/vhost-vsock
```

## Build the image

The build image command must be run at the root of the `android-cuttlefish` repo directory.

Image creation expects cuttlefish debian packages: `cuttlefish-base_*_*64.deb`,
`cuttlefish-user_*_*64.deb` and `cuttlefish-orchestration_*_*64.deb` in the
current directory.

```
podman build \
  --file "tools/testutils/cw/Containerfile" \
  --tag "android-cuttlefish-e2etest:latest" \
  .
```

## Run the container
The run container command must be run at the root of the `android-cuttlefish` repo directory.

```
mkdir -p /tmp/cw_bazel && \
podman run --name tester \
  -d \
  --pids-limit=8192 \
  -v /tmp/cw_bazel:/tmp/cw_bazel \
  -v .:/src/workspace \
  -w /src/workspace/e2etests \
  --cap-add=NET_ADMIN \
  --device=/dev/kvm:/dev/kvm:rwm \
  --device=/dev/net/tun:/dev/net/tun:rwm \
  --device=/dev/vhost-net:/dev/vhost-net:rwm \
  --device=/dev/vhost-vsock:/dev/vhost-vsock:rwm \
  android-cuttlefish-e2etest:latest
```

## Run the test

```
podman exec -it tester \
  bazel --output_user_root=/tmp/cw_bazel/output test //orchestration/journal_gatewayd_test:journal_gatewayd_test_test
```
