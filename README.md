# Virtual Device for Android host-side utilities

This repository holds supporting tools that prepare a host to boot
[Cuttlefish](https://source.android.com/setup/create/cuttlefish), an Android
virtual device designed to run on Google Compute Engine.

The debian packages can be built directly with dpkg-buildpackage to use in any
debian based host. The following script can be used to build host images for
Google Compute Engine:

    device/google/cuttlefish/tools/create_base_image.go

[Check out the AOSP tree](https://source.android.com/setup/build/downloading)
to obtain the script.

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
device.  Such a image allows one to develop for Cuttlefish without having to
install a number of packages directly on their host machine. For more details,
read the [BUILDING.md](docker/BUILDING.md) file.
