# Run e2e tests in Containers

## Podman rootfull containers

**IMPORTANT** Do not use rootfull podman in your development workflow, use rootless podman.

Github Actions does not offer a Debian based runner, using Podman we can create Debian based
containers that mimics a real host behavior.


## Build the image

The build image command must be run at the root of the `android-cuttlefish` repo directory.

Image creation expects cuttlefish debian packages: `cuttlefish-base_*_*64.deb`,
`cuttlefish-user_*_*64.deb` and `cuttlefish-orchestration_*_*64.deb` in the
current directory.

```
sudo podman build \
  --file "tools/testutils/cw/Containerfile" \
  --tag "android-cuttlefish-e2etest:latest" \
  .
```


## Run the container
The run container command must be run at the root of the `android-cuttlefish` repo directory.

```
mkdir -p /tmp/bazel_output && \
sudo podman run \
  --name tester \
  -d \
  --privileged \
  --pids-limit=8192 \
  -v /tmp/bazel_output:/tmp/bazel/output \
  -v .:/src/workspace \
  -w /src/workspace/e2etests \
  android-cuttlefish-e2etest:latest
```

## Run the test

```
sudo podman exec \
  --user=$(id -u):$(id -g) \
  -e "USER=$(whoami)" \
  -it tester \
  bazel --output_user_root=/tmp/bazel/output test //orchestration/journal_gatewayd_test:journal_gatewayd_test_test
```
