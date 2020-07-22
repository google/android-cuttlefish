# Virtual Device for Android host-side utilities

This repository holds source for Debian packages that prepare a host to boot
[Cuttlefish](https://source.android.com/setup/create/cuttlefish), an Android
device that is designed to run on Google Compute Engine.

This package can be built directly with dpkg-buildpackage, but it is
designed to be built with:

    device/google/cuttlefish/tools/create_base_image.sh

[Check out the AOSP tree](https://source.android.com/setup/build/downloading)
to obtain the script.

This repository also contains a Dockerfile that can be used to construct an
image for a privileged Docker container, which in turn can boot the cuttlefish
device.  Such a image allows one to develop for Cuttlefish without having to
install a number of packages directly on their host machine.
