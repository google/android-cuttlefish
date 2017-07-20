# Host-side binaries for Android Virtual Device

## Launcher package

This is the prototype ivshmem-server implementation.

We are breaking from the general philosophy of ivshmem-server inter-vm
communication. In this prototype there is no concept of inter-vm communication;
guests can only talk to daemons running on host.

### Requirements

* Cuttlefish requires the following packages to be installed on your system:
  * binaries
    * python3
    * pip3
    * libvirt-bin
    * libvirt-dev
    * qemu-2.8 or newer
  * python packages (to be installed with pip):
    * argparse
    * glog
    * libvirt-python
    * pylint (development purposes only)

* Users running cuttlefish must be a member of a relevant group enabling them to
  use `virsh` tool, eg. `libvirtd`.
  * Group is created automatically when installing `libvirt-bin` package.
  * Users may need to log out after their membership has been updated; optionally
    you can use `newgrp` to switch currently active group to `libvirtd`.

    ```sh
    sudo usermod -a -G libvirtd $(whoami)
    ```

  * Once configured, users should be able to execute

    ```sh
    $ virsh -c qemu:///system net-list --all
     Name                 State      Autostart     Persistent
    ----------------------------------------------------------
     [...]
    ```

  * You will need to update your configuration `/etc/libvirt/qemu.conf` to disable
    dynamic permission management for image files. Uncomment and modify relevant
    config line:

    ```sh
    dynamic_ownership = 1
    user = "libvirt-qemu"
    group = "kvm"
    # Apparmor would stop us from creating files in /tmp.
    # TODO(ender): find out a better way to manage these permissions.
    security_driver = "none"
    ```

    and restart `libvirt-bin` service:

    ```sh
    sudo service libvirt-bin restart
    ```

### What files should i populate (and where)?

Create a directory to host your files. This directory will need to be accessible
not only by you, but by libvirt, too, and libvirt will likely update ownership
of your files. My recommendation is to use either `/srv/cf` or `/run/cf` folder.

```
mkdir /srv/cf
sudo chown -R libvirt-qemu:root /srv/cf
sudo setfacl -m u:${USER}:rwx /srv/cf
sudo chmod 0770 /srv/cf
```

If you've done the above right, you should be able to create files there, even
if you're not working on behalf of libvirt-qemu user or root group.

You will need to copy (or link) the following files from your build directory:

  * system.img
  * ramdisk.img
  * kernel

This artifact needs to be built and copied from this repo:

  * gce_ramdisk.img

    build: `bazel build //guest:gce_ramdisk`, copy or link file from workspace
    root's bazel-bin folder.

These files need to be manually created:

  * data.img and
  * cache.img

    ```
    truncate -s 10G data.img
    mkfs.ext4 data.img
    truncate -s 2G cache.img
    mkfs.ext4 cache.img
    ```

After you're done linking/copying/creating files, set posix acls on these files
so that you don't lose access to them:

```
setfacl -m u:${USER}:rw /srv/cf/*
```

Done.

### I'm seeing `permission denied` errors

libvirt is not executing virtual machines on behalf of the calling user.
Instead, it calls its own privileged process to configure VM on user's behalf.
If you're seeing `permission denied` errors chances are that the QEmu does
not have access to relevant files _OR folders_.

To work with this problem, it's best to copy (not _link_!) all files QEmu would
need to a separate folder (placed eg. under `/tmp` or `/run`), and give that
folder proper permissions.

```sh
➜ ls -l /srv/cf
total 1569216
drwxr-x---  2 libvirt-qemu eng          180 Jun 28 14:27 .
drwxr-xr-x 45 root         root        2080 Jun 28 14:27 ..
-rwxr-x---  1 root         root  2147483648 Jun 28 14:27 cache.img
-rwxr-x---  1 root         root 10737418240 Jun 28 14:27 data.img
-rwxr-x---  1 root         root      825340 Jun 28 14:27 gce_ramdisk.img
-rwxr-x---  1 root         root     6065728 Jun 28 14:27 kernel
-rwxr-x---  1 root         root     2083099 Jun 28 14:27 ramdisk.img
-rwxr-x---  1 root         root  3221225472 Jun 28 14:27 system.img
```

**Note**: the `/run/cf` folder's owner is `libvirt-qemu`. This allows QEmu
to access images - and me to poke in the folder.

Now don't worry about the `root` ownership. Libvirt manages permissions dynamically.
