# Gigabyte Ampere Cuttlefish Installer

This repo contains the scripts to generate live Debian installer for
cuttlefish for Gigabyte Ampere server.

## Download built image

The built images can be found on https://artifacts.codelinaro.org/ui/native/linaro-372-googlelt-gigabyte-ampere-cuttlefish-installer/gigabyte-ampere-cuttlefish-installer

The latest image are always put into https://artifacts.codelinaro.org/ui/native/linaro-372-googlelt-gigabyte-ampere-cuttlefish-installer/gigabyte-ampere-cuttlefish-installer/latest/

## Scripts

 * build_cf_packages.sh:
   We use this script to build cuttlefish-common packages.
   Please use "pbuilder-dist stable arm64 create" to create a
   chroot environment before running this script.
   And update the chroot environment periodically by
   "pbuilder-dist stable arm64 update".
 * kernel_build_deb.sh:
   * Build AOSP kernel to Debian packages.
   * The source can be downloaded by kernel_download.sh
   * kernel_dependencies.sh install the build dependencies that is needed
     by kernel_build_deb.sh
 * addpreseed.sh:
   * To run this script. Users have to downmiad mini.iso first.
     https://deb.debian.org/debian/dists/bookworm/main/installer-arm64/current/images/netboot/mini.iso
   * This script will add preseed to the mini.iso to make it a live
     installer for Gigabyte Ampere server.

## Modifying the preseed.

We put the preseed file in preseed subdirectory.

 * preseed/preseed.cfg: the preseed file.
 * preseed/after_install_1.sh: the post-install script.

## Metapackage Customization

We have a metapackage. It is in the subdirectory metapackage-linaro-gigamp.
The post-install script of the preseed will install this metapackage.
So some of the customization happened in this metapackage instead of
the post-install script in preseed.

For example:
 * Increasing the ulimit of "open files".
 * Adding NTP servers.
 * Install some extra packages through Depends.

If the customization is not related to boot the machine, we suggest
to move the customization here. It needs a bit of Debian packaging knowledge
though. Adding to post-install script is straightfoward, but it will
bloat the size of the installer image. So we suggest to add any extra
customizations here through Debian-way.

