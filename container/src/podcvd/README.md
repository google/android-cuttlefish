# podcvd

**Note: Currently `podcvd` is very unstable and it's under development. Please
be aware to use.**

`podcvd` is CLI binary which aims to provide identical interface as `cvd`, but
creating each Cuttlefish instance group on a container instance not to
interfere host environment of each other.

## User setup guide

### Build cuttlefish-podcvd debian package

<!-- TODO(seungjaeyoo): Move this into dev guide after it's deployed to AR -->
[tools/buildutils/cw/README.md#container](/tools/buildutils/cw/README.md#container)
describes how to build `cuttlefish-podcvd` debian package.

### Install cuttlefish-podcvd debian package

<!-- TODO(seungjaeyoo): Modify command after it's deployed to AR -->
Execute following command to setup.
```
sudo apt install ./cuttlefish-podcvd_*.deb
/usr/lib/cuttlefish-common/bin/cuttlefish-podcvd-prerequisites.sh
```

Now it's available to execute `podcvd help` or `podcvd create` as you could
execute `cvd help` or `cvd create` after installing `cuttlefish-base`.

## Development guide

### Manually build podcvd

Execute `go build` from `container/src/podcvd` directory.
