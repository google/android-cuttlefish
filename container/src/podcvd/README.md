# podcvd

`podcvd` is a containerized Cuttlefish Virtual Device (CVD) management CLI tool
that runs each Cuttlefish instance group inside an isolated rootless Podman
container. It provides an identical CLI interface to standard `cvd` while
preventing inter-group resource conflicts and host environment interference.

## User setup guide

### podcvd

<!-- TODO(seungjaeyoo): Modify repository after we have deb at stable -->
Execute the following commands to register the APT repository containing the
`cuttlefish-podcvd` package on your machine:

```bash
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://us-apt.pkg.dev/doc/repo-signing-key.gpg -o /etc/apt/keyrings/android-cuttlefish-artifacts.asc
sudo chmod a+r /etc/apt/keyrings/android-cuttlefish-artifacts.asc
echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/android-cuttlefish-artifacts.asc] \
  https://us-apt.pkg.dev/projects/android-cuttlefish-artifacts android-cuttlefish-unstable main" | \
  sudo tee /etc/apt/sources.list.d/android-cuttlefish-artifacts.list > /dev/null
sudo apt update
```

Execute the following commands to install `cuttlefish-podcvd` and configure
your host environment:

```bash
sudo apt install -y cuttlefish-podcvd
podcvd-setup
```

Once setup is complete, you can run `podcvd` commands (such as `podcvd help` or
`podcvd create`) just as you would run `cvd` commands after installing
`cuttlefish-base`.

### Skill for agents

The `podcvd` skill for AI agents is located under the
[`skills/podcvd`](../../../skills/podcvd) directory of this repository.

## Development guide

### Manually build `podcvd` binary

Execute `go build ./cmd/podcvd` from the `container/src/podcvd` directory.

### Manually build `cuttlefish-podcvd` Debian package

Refer to
[`tools/buildutils/cw/README.md#container`](../../../tools/buildutils/cw/README.md#container)
for instructions on building the `cuttlefish-podcvd` Debian package.

Execute the following commands to install the built package and configure your
host:

```bash
sudo apt install ./cuttlefish-podcvd_*.deb
podcvd-setup
```
