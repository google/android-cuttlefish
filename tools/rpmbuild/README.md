## Android Cuttlefish RPM for Red Hat Enterprise Linux

The RPM packages can be built on RHEL 9 with:

    ./tools/buildutils/build_packages.sh

Which calls `build_rpm_spec.sh`, when `dnf` is being detected:

    ./docker/rpm-builder/build_rpm_spec.sh

Or on any OS, within a Docker container, which lives at [`docker/rpm-builder`](https://github.com/google/android-cuttlefish/tree/main/docker/rpm-builder).
