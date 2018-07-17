# Virtual Device for Android host-side utilities

This repository holds source for Debian packages that prepare a host
to boot cuttlefish, an Android device that is designed to run on Google
Compute Engine.

This package can be built directly with dpkg-buildpackage, but it is
designed to be built with:

    device/google/cuttlefish_common/tools/create_base_image.sh

[Check out the AOSP tree](https://source.android.com/setup/build/downloading)
to obtain the script.
