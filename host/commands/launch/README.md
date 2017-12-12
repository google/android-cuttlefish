# Host-side binary for Android Virtual Device

## Launcher package

This is the cuttlefish launcher implementation, that integrates following
features:

* `libvirt` domain configuration,
* `ivshmem` server,
* USB forwarding.

## Overview

### ivshmem

We are breaking from the general philosophy of ivshmem-server inter-vm
communication. In this prototype there is no concept of inter-vm communication;
guests can only talk to daemons running on host.

### Requirements

Cuttlefish requires the following packages to be installed on your system:

Compiling:

* `libjsoncpp-dev`
* `libudev-dev`,
* `libvirt-dev`,
* `libxml2-dev`

Running:

* `linux-image-extra-virtual` to supply `vhci-hcd` module (module must be
  loaded manually)
* `libvirt-bin`
* `libxml2`
* `qemu-2.8` (or newer)

### Building and Installing debian package

To build debian package:

```sh
host$ cd dist
host$ debuild --no-tgz-check -us -uc
host$ cd ..
host$ scp cuttlefish-common*.deb ${USER}@123.45.67.89:
```

This will create file named `cuttlefish-common_0.1-1_amd64.deb` in the root
folder of your workspace. You will have to manually upload this file to
your remote instance and install it as:

```sh
host$ ssh 123.45.67.89
gce$ sudo apt install -f --reinstall ./cuttlefish-common*.deb
```

`apt` will pull in all necessary dependencies. After it's done, support files
will be ready to use.

### Host Configuration

All configuration and basic deployment is covered by scripts in the
`host/deploy` folder. To configure remote instance for cuttlefish, execute:

```sh
host$ cd host/deploy
host$ python main.py config -i 123.45.67.89
```

The script will automatically update libvirt configuration files and user group
membership, as well as create necessary folder for Cuttlefish images.

### Uploading images

To deploy cuttlefish images from build server, execute:

```sh
host$ cd host/deploy
host$ python main.py deploy -i 123.45.67.89
```

By default, the script will pull the latest build of `cf_x86_phone-userdebug`
target from `oc-gce-dev` branch, and latest kernel from cuttlefish kernel
target and branch. Both system and kernel locations can be tuned by supplying
relevant arguments via command line.

Optionally, files can be populated and uploaded manually. Please ensure that
at all times user `libvirt-qemu` can access each of these files by specifying
correct ACL permissions using `setfacl` command, eg:

```sh
gce$ setfacl -m u:libvirt-qemu:rw /path/to/system.img
```

### Starting Cuttlefish

To start cuttlefish, assuming you executed all the above:

```sh
gce$ sudo modprobe vhci-hcd
gce$ sudo cf -system_image_dir /srv/cf/latest/ \
        -kernel /srv/cf/latest/kernel \
        -data_image /srv/cf/latest/data.img \
        -cache_image /srv/cf/latest/cache.img \
        --logtostderr
```

Shortly after, you should be able to execute `adb devices` and see your device
listed. If device is reported as `????????`, or as:

```log
CUTTLEFISHAVD_01	no permissions (verify udev rules); see [http://developer.android.com/tools/device.html]
```

you may have to re-start adb as root (we don't have udev rules updating virtual
usb permissions yet):

```sh
gce$ adb kill-server
gce$ sudo adb devices
```
