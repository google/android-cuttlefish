# Virtual Device for Android host-side utilities

This repository holds supporting tools that prepare a host to boot
[Cuttlefish](https://source.android.com/setup/create/cuttlefish), a configurable
Android Virtual Device (AVD) that targets both locally hosted Linux x86/arm64
and remotely hosted Google Compute Engine (GCE) instances rather than physical
hardware.

## Debian packages

The following debian packages are provided:

* `cuttlefish-base` - Creates static resources needed by the Cuttlefish devices
* `cuttlefish-user` - Provides a local web server that enables interactions with
the devices through the browser
* `cuttlefish-integration` - Installs additional utilities to run Cuttlefish in
Google Compute Engine
* `cuttlefish-orchestration` - Replaces `cuttlefish-user` in the
[Orchestration project](https://github.com/google/cloud-android-orchestration)
* `cuttlefish-common` - [DEPRECATED] Provided for compatibility only, it's a
metapackage that depends on `cuttlefish-base` and `cuttlefish-user`

### Download from Artifact Registry

To register the apt repository on Artifact Registry, please execute command
below.

```bash
sudo curl -fsSL https://us-apt.pkg.dev/doc/repo-signing-key.gpg \
    -o /etc/apt/trusted.gpg.d/artifact-registry.asc
sudo chmod a+r /etc/apt/trusted.gpg.d/artifact-registry.asc
echo "deb https://us-apt.pkg.dev/projects/android-cuttlefish-artifacts android-cuttlefish main" \
    | sudo tee -a /etc/apt/sources.list.d/artifact-registry.list
sudo apt update
```

Afterwards, debian packages are available to download via `apt install`. Noting
that only `cuttlefish-base`, `cuttlefish-user`, and `cuttlefish-orchestration`
are deployed to Artifact Registry.

```bash
sudo apt install cuttlefish-base cuttlefish-user cuttlefish-orchestration
```

### Build and install manually

The packages can be built with the following script:

```bash
tools/buildutils/build_packages.sh
```

Cuttlefish requires only `cuttlefish-base` to be installed, but `cuttlefish-user`
is recommended to enjoy a better user experience. These can be installed after
building with the following commands:

```bash
sudo apt install ./cuttlefish-base_*.deb ./cuttlefish-user_*.deb
sudo usermod -aG kvm,cvdnetwork,render $USER
sudo reboot
```

The last two commands above add the user to the groups necessary to run the Cuttlefish 
Virtual Device and reboot the machine to trigger the installation of additional
kernel modules and apply udev rules.

## Google Compute Engine

The following script can be used to build a host image for Google Compute Engine:

    device/google/cuttlefish/tools/create_base_image.go

[Check out the AOSP tree](https://source.android.com/setup/build/downloading)
to obtain the script.

## Docker

Please read [docker/README.md](docker/README.md) to know how to use docker image
containing Cuttlefish debian packages.
