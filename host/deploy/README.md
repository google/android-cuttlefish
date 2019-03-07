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

* create `cuttlefish` image repository under `/srv/cf` (or a different location
  specified using flags)
* set up `abr0` that will be used to enable networking on Cuttlefish
  instances.

### Deployment

Deployment script requires access to Android Build repository and will not work
unless access is possible.

The script pulls

* System images (`system.img` and `ramdisk.img`) from specified
  branch/target/build combination
* Kernel image from specified branch/target/build combination

and uploads these files to a specified instance to cuttlefish image folder.
