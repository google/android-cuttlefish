require recipes-products/images/qcom-console-image.bb

SUMMARY = "Designed to run vanilla CF on QC HW"

LICENSE = "MIT"
REQUIRED_DISTRO_FEATURES += "wayland"

CORE_IMAGE_BASE_INSTALL += " \
    packagegroup-cuttlefish \
"
