# podcvd

**Note: Currently `podcvd` is very unstable and it's under development. Please
be aware to use.**

`podcvd` is CLI binary which aims to provide identical interface as `cvd`, but
creating each Cuttlefish instance group on a container instance not to
interfere host environment of each other.

## User setup guide

### podcvd

<!-- TODO(seungjaeyoo): Modify repository after we have deb at stable/unstable -->
Execute following commands to register apt repository containing
`cuttlefish-podcvd` package on your machine.
```
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://us-apt.pkg.dev/doc/repo-signing-key.gpg -o /etc/apt/keyrings/android-cuttlefish-artifacts.asc
sudo chmod a+r /etc/apt/keyrings/android-cuttlefish-artifacts.asc
echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/android-cuttlefish-artifacts.asc] \
  https://us-apt.pkg.dev/projects/android-cuttlefish-artifacts android-cuttlefish-nightly main" | \
  sudo tee /etc/apt/sources.list.d/android-cuttlefish-artifacts.list > /dev/null
sudo apt update
```

Execute following commands to install `cuttlefish-podcvd` and setup your
machine.
```
sudo apt install cuttlefish-podcvd
/usr/lib/cuttlefish-podcvd/bin/cuttlefish-podcvd-prerequisites.sh
```

Now it's available to execute `podcvd help` or `podcvd create` as you could
execute `cvd help` or `cvd create` after installing `cuttlefish-base`.

### cuttlefish-mcp-server

cuttlefish-mcp-server requires additional setup beyond podcvd.

Execute `gemini extensions install /usr/lib/cuttlefish-podcvd/cuttlefish-mcp-server/`.

## Development guide

### Manually build podcvd binary

Execute `go build ./cmd/podcvd` from `container/src/podcvd` directory.

### Manually build cuttlefish-podcvd debian package

[tools/buildutils/cw/README.md#container](/tools/buildutils/cw/README.md#container)
describes how to build `cuttlefish-podcvd` debian package.

Execute `sudo apt install ./cuttlefish-podcvd_*.deb` to install it on your
machine.

### Manually build cuttlefish_mcp_server

Execute `go build ./cmd/cuttlefish_mcp_server` from `container/src/podcvd`
directory.

### Test cuttlefish_mcp_server on your local machine

Execute `gemini extensions install container/src/podcvd`.
