# Configuration and Deployment scripts for Cuttlefish.

This folder contains a basic utility that configures and uploads Cuttlefish
images to remote GCE instance.

## Basic Usage

### Configuration

All configuration and basic deployment is covered by scripts in the
`host/deploy` folder. To configure remote instance for cuttlefish, execute:

```
python main.py config -i 123.45.67.89
```

The script will automatically update libvirt configuration files and user group
membership, as well as create necessary folder for Cuttlefish images.

### Deployment

To deploy cuttlefish images from build server, execute:

```
python main.py deploy -i 123.45.67.89
```

By default, the script will pull the latest build of `cf_x86_phone-userdebug`
target from `oc-gce-dev` branch, and latest kernel from cuttlefish kernel
target and branch. Both system and kernel locations can be tuned by supplying
relevant arguments via command line.

## Detailed execution

### Configuration

Summary of the changes applied by config stage:

* add current user to `libvirt` group,
* create `cuttlefish` image repository under `/srv/cf` (or a different location
  specified using flags)
* set up `abr0` that will be used to enable networking on Cuttlefish
  instances.
* grants `libvirt-qemu` user access to `/srv/cf` (or a different, user-specified
  location, that will later be hosting cuttlefish system images)
* update `/etc/libvirt/qemu.conf`:
  * set `dynamic_ownership` to `0` to prevent libvirt from manipulating file
    permissions,
  * set `security_driver` to `none` to allow QEmu instances to connect to our
    ivshmem socket.
* finally, restarts `libvirt` to apply changes.

### Deployment

Deployment script requires access to Android Build repository and will not work
unless access is possible.

The script pulls

* System images (`system.img` and `ramdisk.img`) from specified
  branch/target/build combination
* Kernel image from specified branch/target/build combination

and uploads these files to a specified instance to cuttlefish image folder.
