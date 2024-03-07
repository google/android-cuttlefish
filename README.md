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

The packages can be built with the following command:

```bash
sudo apt install devscripts equivs
for dir in base frontend; do
    pushd $dir
    # Install build dependencies
    sudo mk-build-deps -i
    dpkg-buildpackage -uc -us
    popd
done
```

Cuttlefish requires only `cuttlefish-base` to be installed, but `cuttlefish-user`
is recommended to enjoy a better user experience. These can be installed after
building with the following command:

```bash
sudo apt install ./cuttlefish-base_*.deb ./cuttlefish-user_*.deb
```

## Google Compute Engine

The following script can be used to build a host image for Google Compute Engine:

    device/google/cuttlefish/tools/create_base_image.go

[Check out the AOSP tree](https://source.android.com/setup/build/downloading)
to obtain the script.

## Docker

The Debian packages can also be built in a the docker container. Dockerfile and
build scripts are included in this repository, just run:

```
    docker/build.sh --build_debs_only --rebuild_debs_verbose
```

The command will build the container, if needed, that builds the Debian packages,
and builds the Debian packages within the container. The resultant packages will
be located under the ```docker/out``` directory.
 
This repository also contains a Dockerfile that can be used to construct an
image for a privileged Docker container, which in turn can boot the cuttlefish
device.  Such an image allows one to develop for Cuttlefish without having to
install a number of packages directly on the host machine. For more details,
read the [docker instructions](docker/README.md).
