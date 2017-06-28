# Host-side binaries for Android Virtual Device

## Launcher package

### Requirements

* Cuttlefish requires the following packages to be installed on your system:
  * binaries
    * python3
    * libvirt-bin
    * libvirt-dev
    * qemu-2.8 or newer
  * python packages (to be installed with pip):
    * argparse
    * eventfd
    * glog
    * linuxfd
    * libvirt
    * posix_ipc
    * pylint (development purposes only)

* Users running cuttlefish must be a member of a relevant group enabling them to
  use `virsh` tool, eg. `libvirtd`.
  * Group is created automatically when installing `libvirt-bin` package.
  * Users may need to log out after their membership has been updated; optionally
    you can use `newgrp` to switch currently active group to `libvirtd`.
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

### I'm seeing `permission denied` errors

libvirt is not executing virtual machines on behalf of the calling user.
Instead, it calls its own privileged process to configure VM on user's behalf.
If you're seeing `permission denied` errors chances are that the QEmu does
not have access to relevant files _OR folders_.

To work with this problem, it's best to copy (not _link_!) all files QEmu would
need to a separate folder (placed eg. under `/tmp` or `/run`), and give that
folder proper permissions.

```sh
➜ ls -l /run/cf
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

**Note**: the `/run/cf` folder's owner is `libvirt-qemu:eng`. This allows QEmu
to access images - and me to poke in the folder.

Now don't worry about the `root` ownership. Libvirt manages permissions dynamically.
You may want to give yourself write permissions to these files during development,
though.
