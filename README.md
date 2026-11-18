# Virtual Device for Android host-side utilities

This repository holds supporting tools that prepare a host to boot
[Cuttlefish](https://source.android.com/setup/create/cuttlefish), a configurable
Android Virtual Device (AVD) that targets both locally hosted Linux x86/arm64
and remotely hosted Google Compute Engine (GCE) instances rather than physical
hardware.

## Developing the host tools

To build and run the host tools directly from source, see
[Getting started](docs/GettingStarted.md).

## Debian packages

This repository builds the following Debian packages:

* `cuttlefish-base` - Creates static resources needed by the Cuttlefish devices
* `cuttlefish-user` - Provides a local web server that enables interactions with
the devices through the browser
* `cuttlefish-integration` - Installs additional utilities to run Cuttlefish in
Google Compute Engine
* `cuttlefish-orchestration` - Replaces `cuttlefish-user` in the
[Orchestration project](https://github.com/google/cloud-android-orchestration)
* `cuttlefish-common` - [DEPRECATED] Provided for compatibility only, it's a
metapackage that depends on `cuttlefish-base` and `cuttlefish-user`

### Installing the packages

`cuttlefish-base`, `cuttlefish-user`, and `cuttlefish-orchestration` are
published to an apt repository on Artifact Registry. Register it with:

```bash
sudo curl -fsSL https://us-apt.pkg.dev/doc/repo-signing-key.gpg \
    -o /etc/apt/trusted.gpg.d/artifact-registry.asc
sudo chmod a+r /etc/apt/trusted.gpg.d/artifact-registry.asc
echo "deb https://us-apt.pkg.dev/projects/android-cuttlefish-artifacts android-cuttlefish main" \
    | sudo tee -a /etc/apt/sources.list.d/artifact-registry.list
sudo apt update
```

Cuttlefish requires only `cuttlefish-base`, but `cuttlefish-user` is
recommended for a better user experience.

```bash
sudo apt install cuttlefish-base cuttlefish-user
sudo usermod -aG kvm,cvdnetwork,render $USER
```

The last command adds the user to the groups necessary to run the
Cuttlefish Virtual Device. No reboot or new login is needed; `cvd` picks up
the new group membership itself.

### Building the packages

The packages can be built from this repository with:

```bash
tools/buildutils/build_packages.sh
```

The resulting `.deb` files can be installed with `sudo apt install ./<file>`
in place of the Artifact Registry packages above.

## See also

* [Container images](container/README.md) with the Cuttlefish packages
  preinstalled, for use with docker or podman.
* [Google Compute Engine images](tools/baseimage/README.md) for running
  Cuttlefish on GCE.
