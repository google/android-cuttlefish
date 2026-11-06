# podcvd

`podcvd` is a containerized Cuttlefish Virtual Device (CVD) management CLI tool
that runs each Cuttlefish instance group inside an isolated rootless Podman
container. It provides an identical CLI interface to standard `cvd` while
preventing inter-group resource conflicts and host environment interference.

Each instance group runs in its own container created from the
`cuttlefish-orchestration` [container image](../../README.md), so no Cuttlefish
host packages other than `cuttlefish-podcvd` are required on the host.

## User setup guide

### podcvd

`cuttlefish-podcvd` requires Podman 4.4 or later, which is available on Debian
13 (trixie) or later.

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

Replace `android-cuttlefish-unstable` with `android-cuttlefish` or
`android-cuttlefish-nightly` in the commands above to use the stable or nightly
channel instead.

Execute the following commands to install `cuttlefish-podcvd` and configure
your host environment:

```bash
sudo apt install -y cuttlefish-podcvd
podcvd-setup
```

`podcvd-setup` configures the current user for rootless Podman, grants access to
`/dev/kvm`, `/dev/vhost-net` and `/dev/vhost-vsock`, and allocates a private
subnet for the user's containers. Run `sudo podcvd-setup <username>` to
configure another user.

Once setup is complete, you can run `podcvd` commands (such as `podcvd help` or
`podcvd create`) just as you would run `cvd` commands after installing
`cuttlefish-base`.

### Separating instance groups per client

Set the `PODCVD_CLIENT_ID` environment variable to separate the instance groups
of multiple clients (e.g. multiple agents) sharing the same user. `podcvd`
commands only see the instance groups created with the same `PODCVD_CLIENT_ID`.
Instance groups created without `PODCVD_CLIENT_ID` are visible to all clients.

```bash
PODCVD_CLIENT_ID=client-a podcvd create
PODCVD_CLIENT_ID=client-a podcvd fleet
```

### Skill for agents

The `podcvd` skill for AI agents is located under the
[`skills/podcvd`](../../../skills/podcvd) directory of this repository.
You can modify the skill to apply `PODCVD_CLIENT_ID` described above, to
separate the instance groups of each agent.

## Development guide

### Manually build `podcvd` binary

Execute `go build ./cmd/podcvd` from the `container/src/podcvd` directory.

### Run unit tests

Execute `go test ./...` from the `container/src/podcvd` directory.

### Manually build `cuttlefish-podcvd` Debian package

Refer to
[`tools/buildutils/cw/README.md#container`](../../../tools/buildutils/cw/README.md#container)
for instructions on building the `cuttlefish-podcvd` Debian package, including
building it with a custom container image.

Execute the following commands to install the built package and configure your
host:

```bash
sudo apt install ./cuttlefish-podcvd_*.deb
podcvd-setup
```
