SUMMARY = "Cuttlefish packages"
DESCRIPTION = "Packages needed to enable Cuttlefish Android Virtual Devices (AVDs)"

LICENSE = "MIT"

PACKAGE_ARCH = "${TUNE_PKGARCH}"

inherit packagegroup

PROVIDES = "${PACKAGES}"
PACKAGES = "${PN}"

RDEPENDS:${PN} = "\
    crosvm \
    "
